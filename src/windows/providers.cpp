#include "windows/providers.hpp"
#include "windows/discovery.hpp"
#include "core/codex.hpp"
#include "core/claude.hpp"
#include <windows.h>
#include <json.hpp>
#include <filesystem>
#include <stdexcept>
#include <algorithm>

namespace usage::windows {
namespace {
struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring environment(const wchar_t* name) {
    const DWORD size=GetEnvironmentVariableW(name,nullptr,0);
    if (!size) return {};
    std::wstring value(size,L'\0');
    const DWORD written=GetEnvironmentVariableW(name,value.data(),size);
    if (!written || written>=size) return {};
    value.resize(written); return value;
}
std::filesystem::path unquote(std::wstring value) {
    if (value.size()>1 && value.front()==L'"' && value.back()==L'"') value=value.substr(1,value.size()-2);
    return value;
}
DiscoveryResult executable(Service service) {
    const bool claude=service==Service::Claude;
    DiscoveryLocations locations;
    locations.override_path=unquote(environment(claude ? L"USAGETRACKER_CLAUDE" : L"USAGETRACKER_CODEX"));
    const auto search=environment(L"PATH");
    for (std::size_t start=0; start<search.size();) {
        auto end=search.find(L';',start); if (end==std::wstring::npos) end=search.size();
        const auto part=unquote(search.substr(start,end-start));
        if (!part.empty()) locations.bins.push_back(part);
        start=end+1;
    }
    auto npm=[&](const std::filesystem::path& prefix) {
        if (prefix.empty()) return;
        const auto modules=prefix/L"node_modules";
        locations.packages.push_back(modules/(claude ? L"@anthropic-ai" : L"@openai")/(claude ? L"claude-code" : L"codex"));
        // Recent npm releases install platform binaries as sibling packages.
        locations.packages.push_back(modules/(claude ? L"@anthropic-ai" : L"@openai")/(claude ? L"claude-code-win32-x64" : L"codex-win32-x64"));
    };
    for (const auto& bin : locations.bins) {
        npm(bin);
        // Portable VS Code/Insiders installations expose their bin folder on PATH.
        locations.extensions.push_back(bin.parent_path()/L"data/extensions");
    }
    const auto home=environment(L"USERPROFILE"), local=environment(L"LOCALAPPDATA"), roaming=environment(L"APPDATA");
    if (!home.empty()) {
        const std::filesystem::path root(home);
        for (const auto* bin : {L".local/bin",L".cargo/bin",L"scoop/shims"}) locations.bins.push_back(root/bin);
        for (const auto* editor : {L".vscode",L".vscode-insiders",L".vscode-oss",L".cursor",L".windsurf"}) locations.extensions.push_back(root/editor/L"extensions");
        locations.packages.push_back(root/L"scoop/apps"/(claude ? L"claude-code" : L"codex")/L"current");
    }
    if (!roaming.empty()) npm(std::filesystem::path(roaming)/L"npm");
    if (!local.empty()) {
        npm(std::filesystem::path(local)/L"npm");
        locations.bins.push_back(std::filesystem::path(local)/L"Microsoft/WinGet/Links");
    }
    for (const auto* variable : {L"NPM_CONFIG_PREFIX",L"VOLTA_HOME",L"NVM_SYMLINK"}) npm(environment(variable));
    for (const auto* variable : {L"VSCODE_EXTENSIONS",L"VSCODE_EXTENSIONS_DIR"}) {
        const auto root=environment(variable); if (!root.empty()) locations.extensions.push_back(root);
    }
    const auto portable=environment(L"VSCODE_PORTABLE");
    if (!portable.empty()) locations.extensions.push_back(std::filesystem::path(portable)/L"extensions");
    return discover_executable(claude,locations);
}
AccountUsage read_limits(const std::atomic<bool>& stop, const std::filesystem::path& exe, Service service) {
    const bool claude = service == Service::Claude;
    static std::mutex spawn_mutex;
    std::unique_lock<std::mutex> spawn_lock(spawn_mutex);
    Handle input_read,input_write,output_read,output_write,null_output,process,thread,job;
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    if (!CreatePipe(&input_read.value,&input_write.value,&security,0) ||
        !CreatePipe(&output_read.value,&output_write.value,&security,0)) throw std::runtime_error("Could not create usage pipes");
    SetHandleInformation(input_write.value,HANDLE_FLAG_INHERIT,0);
    SetHandleInformation(output_read.value,HANDLE_FLAG_INHERIT,0);
    null_output.value = CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input_read.value; startup.hStdOutput=output_write.value; startup.hStdError=null_output.value;
    PROCESS_INFORMATION info{};
    std::wstring command=L"\""+exe.wstring()+L"\"" + (claude ? L" --print --input-format stream-json --output-format stream-json --verbose --no-session-persistence --safe-mode --strict-mcp-config" : L" app-server --listen stdio://");
    job.value=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) throw std::runtime_error("Could not manage usage process");
    if (!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&info)) throw std::runtime_error("Could not start usage helper");
    process.value=info.hProcess; thread.value=info.hThread;
    if (!AssignProcessToJobObject(job.value,process.value)) { TerminateProcess(process.value,1); throw std::runtime_error("Could not manage usage process"); }
    ResumeThread(thread.value);
    CloseHandle(input_read.value); input_read.value=nullptr;
    CloseHandle(output_write.value); output_write.value=nullptr;
    spawn_lock.unlock();
    auto send=[&](const nlohmann::json& message) {
        const auto line=message.dump()+"\n";
        DWORD written{};
        if (!WriteFile(input_write.value,line.data(),static_cast<DWORD>(line.size()),&written,nullptr) || written != line.size()) throw std::runtime_error("Usage connection closed");
    };
    if (claude) send({{"type","control_request"},{"request_id","init"},{"request",{{"subtype","initialize"}}}});
    else send({{"id",1},{"method","initialize"},{"params",{{"clientInfo",{{"name","usage_tracker"},{"version","0.4.0"}}}}}});
    std::string buffer;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while (!stop && std::chrono::steady_clock::now()<deadline) {
        DWORD available{};
        if (!PeekNamedPipe(output_read.value,nullptr,0,nullptr,&available,nullptr)) throw std::runtime_error("Usage connection closed");
        if (!available) { if (WaitForSingleObject(process.value,50)==WAIT_OBJECT_0) throw std::runtime_error("Usage helper exited before replying"); continue; }
        char bytes[4096]; DWORD read{};
        if (!ReadFile(output_read.value,bytes,sizeof(bytes),&read,nullptr)) throw std::runtime_error("Usage connection closed");
        buffer.append(bytes,read);
        if (buffer.size()>1024*1024) throw std::runtime_error("Usage reply too large");
        for (auto newline=buffer.find('\n'); newline!=std::string::npos; newline=buffer.find('\n')) {
            auto message=nlohmann::json::parse(buffer.substr(0,newline)); buffer.erase(0,newline+1);
            if (claude) {
                if (message.value("type",std::string{}) != "control_response") continue;
                const auto& response = message.at("response");
                const auto id = response.value("request_id",std::string{});
                if (id != "init" && id != "usage") continue;
                if (response.value("subtype",std::string{}) != "success") throw std::runtime_error("Usage unavailable; check Claude login/version");
                if (id == "init") send({{"type","control_request"},{"request_id","usage"},{"request",{{"subtype","get_usage"}}}});
                else return parse_claude_limits(response.at("response").dump());
                continue;
            }
            if (!message.contains("id") || !message["id"].is_number_integer()) continue;
            const int id=message["id"].get<int>();
            if (id!=1 && id!=2) continue;
            if (message.contains("error")) throw std::runtime_error("Usage unavailable; check your Codex login");
            if (id==1) {
                send({{"method","initialized"},{"params",nlohmann::json::object()}});
                send({{"id",2},{"method","account/rateLimits/read"},{"params",nlohmann::json::object()}});
            } else return parse_codex_limits(message.at("result").dump());
        }
    }
    throw std::runtime_error("Usage request timed out");
}
}
void detect_service(Service service, AccountUsage& result) {
    try {
        const auto detection=executable(service);
        const auto& path=detection.path;
        if (path.empty() || !result.installed) result.error=detection.error;
        result.installed=!path.empty();
        const auto utf8=path.u8string();
        result.executable_path.assign(utf8.begin(),utf8.end());
    } catch (const std::exception& error) {
        result.installed=false; result.executable_path.clear(); result.error=error.what();
    }
}
void UsageReader::set_interval(int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    seconds=std::clamp(seconds,15,900);
    if (interval_seconds_==seconds) return;
    interval_seconds_=seconds; interval_changed_=true; wake_.notify_all();
}
UsageReader::UsageReader(Service service, AccountUsage initial) : service_(service), latest_(std::move(initial)), worker_([this] { run(); }) {}
UsageReader::~UsageReader() { stop_=true; wake_.notify_all(); if (worker_.joinable()) worker_.join(); }
void UsageReader::refresh() { std::lock_guard<std::mutex> lock(mutex_); requested_=true; wake_.notify_all(); }
bool UsageReader::take(AccountUsage& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!changed_) return false;
    result=latest_; changed_=false; return true;
}
void UsageReader::run() {
    while (!stop_) {
        { std::lock_guard<std::mutex> lock(mutex_); requested_=false; }
        try {
            const auto detection=executable(service_);
            const auto& exe=detection.path;
            AccountUsage result;
            result.installed=!exe.empty();
            result.error=detection.error;
            if (result.installed) {
                { std::lock_guard<std::mutex> lock(mutex_); latest_.installed=true;
                    const auto path=exe.u8string(); latest_.executable_path.assign(path.begin(),path.end()); changed_=true; }
                result=read_limits(stop_,exe,service_);
            }
            const auto path=exe.u8string(); result.executable_path.assign(path.begin(),path.end());
            std::lock_guard<std::mutex> lock(mutex_); latest_=std::move(result); changed_=true;
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(mutex_);
            // Keep the last valid reading, but never pass it off as fresh data.
            latest_.error=error.what(); changed_=true;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stop_ && !requested_) {
            interval_changed_=false;
            if (!wake_.wait_for(lock,std::chrono::seconds(interval_seconds_),[this] { return stop_ || requested_ || interval_changed_; })) break;
        }
    }
}
}
