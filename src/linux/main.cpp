#include "host/platform.hpp"
#include "linux/app.hpp"
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/file.h>
#include <unistd.h>

namespace {
// One instance per user, as the Windows build's named mutex does: a lock file
// held for the process lifetime, released by the kernel however it exits.
bool claim_instance(const std::string& name) {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    const std::string directory = runtime && *runtime ? runtime : "/tmp";
    const auto path = directory + "/" + name + "-" + std::to_string(getuid()) + ".lock";
    const int descriptor = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    return descriptor >= 0 && flock(descriptor, LOCK_EX | LOCK_NB) == 0;
}
} // namespace

int main(int argc, char** argv) {
    bool smoke = false, reset = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        smoke = smoke || argument == "--smoke-test";
        reset = reset || argument == "--reset";
    }
#ifdef USAGETRACKER_MOCK
    // The debug build runs beside an installed copy, so it takes its own lock.
    std::string instance = "UsageTrackerDebug";
#else
    std::string instance = "UsageTracker";
#endif
    // The self-test must not collide with a running copy.
    if (smoke)
        instance += "-smoke";
    if (!claim_instance(instance)) {
        std::cerr << "UsageTracker is already running.\n";
        return 2;
    }
    try {
#ifdef USAGETRACKER_MOCK
        constexpr bool mock_build = true;
#else
        constexpr bool mock_build = false;
#endif
        // Start over from the defaults: settings and the widget's place.
        if (reset && !smoke && !usage::linux_host::App::reset_configuration(mock_build))
            std::cerr << "UsageTracker: could not remove the saved configuration.\n";
        std::shared_ptr<usage::host::MockProviders> mock;
#ifdef USAGETRACKER_MOCK
        // Smoke tests keep their fixed demo data; middle-click cycles scenarios otherwise.
        if (!smoke)
            mock = std::make_shared<usage::host::MockProviders>(usage::mock_presets().front().scenario);
#endif
        usage::linux_host::App app(smoke, mock);
        return app.run();
    } catch (const std::exception& error) {
        usage::host::log(std::string("Fatal error: ") + error.what());
        std::cerr << "UsageTracker: " << error.what() << '\n';
        return 1;
    }
}
