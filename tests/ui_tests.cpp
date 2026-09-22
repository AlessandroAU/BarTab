#include "ui/views.hpp"
#include <cmath>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Clay_Dimensions measure(Clay_StringSlice text, Clay_TextElementConfig* config, void*) {
    return {static_cast<float>(text.length)*config->fontSize*0.5f,static_cast<float>(config->fontSize)};
}
ClayWidgets_Input neutral() { ClayWidgets_Input result{}; result.mouseX = result.mouseY = -100; return result; }
bool contains(Clay_RenderCommandArray commands, const std::string& expected) {
    for (int i = 0; i < commands.length; ++i) {
        const auto& command = commands.internalArray[i];
        if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            const auto& text = command.renderData.text.stringContents;
            if (std::string(text.chars,static_cast<std::size_t>(text.length)) == expected) return true;
        }
    }
    return false;
}
}
int main() {
    try {
        usage::Usage data;
        usage::ui::View details(usage::ui::Surface::Details,measure,nullptr);
        usage::ui::View widget(usage::ui::Surface::Widget,measure,nullptr);
        usage::ui::View hover(usage::ui::Surface::Hover,measure,nullptr);
        usage::Preferences baseline; baseline.appearance.text_percent=100; baseline.appearance.widget_width=208;
        widget.set_preferences(baseline); hover.set_preferences(baseline);
        usage::Usage live;
        live.live=true;
        auto loading=widget.frame(live,neutral(),208,38);
        check(contains(loading.commands,"Codex: connecting..."),"No invented allowance while loading");
        live.account.windows.push_back({"Weekly",85,1790583271});
        auto actual=widget.frame(live,neutral(),208,38);
        check(contains(actual.commands,"Codex") && contains(actual.commands,"85%") && !contains(actual.commands,"62%"),"Single primary weekly allowance displayed");
        live.account.error="Connection failed";
        actual=widget.frame(live,neutral(),208,38);
        check(contains(actual.commands,"Codex *"),"Failed refresh marks cached data stale");
        for (bool light : {true,false}) {
            widget.set_system_light(light);
            actual=widget.frame(live,neutral(),208,38);
            bool saw_text=false;
            for (int i=0;i<actual.commands.length;++i) {
                const auto& command=actual.commands.internalArray[i];
                if (command.commandType!=CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
                const auto tint=command.renderData.text.textColor;
                check(light ? tint.r<100 && tint.g<100 && tint.b<100 : tint.r>100,"Taskbar labels and reset text follow the system theme in both directions");
                saw_text=true;
            }
            check(saw_text,"Theme test renders taskbar text");
        }
        live.claude.installed=true;
        live.claude.windows={{"5 hour",97,0},{"Weekly",44,0},{"Fable weekly",23,0}};
        actual=widget.frame(live,neutral(),208,38);
        check(contains(actual.commands,"Codex *") && contains(actual.commands,"Claude") && contains(actual.commands,"85%") && contains(actual.commands,"44 / 23%"),"Claude shows overall and Fable percentages separately");
        auto resets=live;
        std::tm day{}; day.tm_year=126; day.tm_mon=8; day.tm_mday=25; day.tm_hour=12; day.tm_isdst=-1;
        resets.claude.windows[1].resets_at=std::mktime(&day);
        resets.claude.windows[2].resets_at=resets.claude.windows[1].resets_at+3600;
        actual=widget.frame(resets,neutral(),208,38);
        check(contains(actual.commands,"reset 25/09"),"Claude merges resets on the same local day despite different times");
        resets.codex_enabled=false;
        for (int percent : {100,150,300}) {
            widget.set_text_percent(percent);
            const auto size=usage::ui::widget_size(resets,widget.preferences().appearance);
            actual=widget.frame(resets,neutral(),size.width,size.height);
            int reset_labels=0;
            for (int i=0;i<actual.commands.length;++i) {
                const auto& command=actual.commands.internalArray[i];
                if (command.commandType!=CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
                const auto& value=command.renderData.text.stringContents;
                if (std::string(value.chars,static_cast<std::size_t>(value.length)).find("reset ")==0) ++reset_labels;
            }
            check(reset_labels==0 && contains(actual.commands,"25/09") && contains(actual.commands,"12:00/13:00"),"Claude-only reset column shares the date and preserves both times");
            check(std::abs(widget.bounds("ClaudeGeneralTrack").width-widget.bounds("ClaudeFableTrack").width)<0.1f,"Claude-only tracks have equal widths");
        }
        widget.set_preferences(baseline);
        resets.codex_enabled=true;
        resets.claude.windows[2].resets_at+=86400;
        actual=widget.frame(resets,neutral(),208,38);
        check(contains(actual.commands,"reset 25/09 / 26/09"),"Claude retains different reset dates");
        for (const int percent : {100,140}) {
            hover.set_text_percent(percent);
            const auto size=usage::ui::hover_size(live,percent);
            actual=hover.frame(live,neutral(),size.width,size.height);
            check(contains(actual.commands,"Codex - stale") && contains(actual.commands,"Claude") && contains(actual.commands,"Fable weekly"),"Combined hover preserves providers, stale state and model windows");
            const auto codex=hover.bounds("HoverCodex"), claude=hover.bounds("HoverClaude");
            check(codex.x==claude.x && claude.y>=codex.y+codex.height,"Providers stack in one compact panel");
            check(size.width<=504 && size.height<340,"Combined hover stays compact at both text sizes");
            for (int i=0; i<actual.commands.length; ++i) {
                const auto& command=actual.commands.internalArray[i];
                if (command.commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
                    check(command.boundingBox.x>=0 && command.boundingBox.x+command.boundingBox.width<=size.width,"Hover text fits horizontally");
                    check(command.boundingBox.y>=0 && command.boundingBox.y+command.boundingBox.height<=size.height,"Hover text fits vertically");
                }
            }
        }
        hover.set_text_percent(100);
        // Saved provider choices control all surfaces, even with retained readings.
        auto selected=live;
        selected.claude.windows[1].resets_at=1790583271;
        selected.claude.windows[2].resets_at=1790669671;
        for (const bool codex : {false,true}) for (const bool claude : {false,true}) {
            selected.codex_enabled=codex;
            selected.claude_enabled=claude;
            for (const int percent : {100,140}) {
                widget.set_text_percent(percent);
                const float width=usage::widget_width*percent/100.f;
                actual=widget.frame(selected,neutral(),width,38);
                check(contains(actual.commands,"Codex *")==codex,"Codex visibility follows preference");
                if (claude && !codex) {
                    check(contains(actual.commands,"General") && contains(actual.commands,"Fable"),"Claude-only labels both allowances");
                    check(contains(actual.commands,"44%") && contains(actual.commands,"23%"),"Claude-only rows include separate percentages");
                    check(widget.bounds("ClaudeGeneral").y < widget.bounds("ClaudeFable").y,"General and Fable have separate taskbar rows");
                }
                if (!codex && !claude) check(contains(actual.commands,"Providers disabled - open settings"),"All-disabled state stays actionable");
                for (int i=0; i<actual.commands.length; ++i) {
                    const auto& command=actual.commands.internalArray[i];
                    if (command.commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
                        check(command.boundingBox.x>=0 && command.boundingBox.x+command.boundingBox.width<=width,"Taskbar labels fit enabled-provider layout");
                        check(command.boundingBox.y>=0 && command.boundingBox.y+command.boundingBox.height<=38,"Taskbar rows fit at all text sizes");
                    }
                }
            }
            const auto size=usage::ui::hover_size(selected,100);
            actual=hover.frame(selected,neutral(),size.width,size.height);
            check(contains(actual.commands,"Codex - stale")==codex && contains(actual.commands,"Claude")==claude,"Disabled providers disappear from hover");
        }
        auto narrow_usage=live;
        narrow_usage.account.windows={{"Weekly",100,1790583271}};
        narrow_usage.claude.windows={{"Weekly",100,1790583271},{"Fable weekly",100,1790669671}};
        narrow_usage.claude.error="Offline";
        for (const int percent : {100,140}) {
            usage::Preferences narrow;
            narrow.appearance.widget_width=160; narrow.appearance.hover_width=240; narrow.appearance.text_percent=percent;
            widget.set_preferences(narrow); hover.set_preferences(narrow);
            for (const bool codex : {false,true}) {
                narrow_usage.codex_enabled=codex;
                const float width=160*percent/100.f;
                actual=widget.frame(narrow_usage,neutral(),width,38);
                for (int i=0; i<actual.commands.length; ++i) {
                    const auto& command=actual.commands.internalArray[i];
                    if (command.commandType==CLAY_RENDER_COMMAND_TYPE_TEXT)
                        check(command.boundingBox.x>=0 && command.boundingBox.x+command.boundingBox.width<=width,"Small widget fits percentages, stale labels and reset dates");
                }
            }
            const auto height=usage::ui::hover_size(narrow_usage,percent).height;
            const float width=240*percent/100.f;
            actual=hover.frame(narrow_usage,neutral(),width,height);
            for (int i=0; i<actual.commands.length; ++i) {
                const auto& command=actual.commands.internalArray[i];
                if (command.commandType==CLAY_RENDER_COMMAND_TYPE_TEXT)
                    check(command.boundingBox.x>=0 && command.boundingBox.x+command.boundingBox.width<=width,"Small hover card keeps labels readable");
            }
        }
        for (const int percent : {100,150,160,170,200,300}) {
            usage::Preferences sized; sized.appearance.text_percent=percent;
            widget.set_preferences(sized); hover.set_preferences(sized);
            for (const bool codex : {false,true}) {
                narrow_usage.codex_enabled=codex;
                const auto size=usage::ui::widget_size(narrow_usage,sized.appearance);
                actual=widget.frame(narrow_usage,neutral(),size.width,size.height);
                if (!codex && percent>160) check(widget.bounds("ClaudeGeneral").x<widget.bounds("ClaudeFable").x,"Large taskbar text uses a horizontal layout");
                for (int i=0; i<actual.commands.length; ++i) {
                    const auto& command=actual.commands.internalArray[i];
                    if (command.commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
                        check(command.boundingBox.x>=0 && command.boundingBox.x+command.boundingBox.width<=size.width,"Large taskbar text fits horizontally");
                        check(command.boundingBox.y>=0 && command.boundingBox.y+command.boundingBox.height<=size.height,"Large taskbar text fits vertically");
                    }
                }
            }
            const auto size=usage::ui::hover_size(narrow_usage,percent);
            actual=hover.frame(narrow_usage,neutral(),size.width,size.height);
            for (int i=0; i<actual.commands.length; ++i) {
                const auto& command=actual.commands.internalArray[i];
                if (command.commandType==CLAY_RENDER_COMMAND_TYPE_TEXT)
                    check(command.boundingBox.y+command.boundingBox.height<=size.height,"Hover accommodates the entire text-size range");
            }
        }
        widget.set_preferences(baseline); hover.set_preferences(baseline);
        live.account.installed=false;
        actual=widget.frame(live,neutral(),208,38);
        check(!contains(actual.commands,"Codex *") && contains(actual.commands,"General") && contains(actual.commands,"Fable"),"Claude alone uses separate General and Fable rows");
        live.claude.installed=false;
        actual=widget.frame(live,neutral(),208,38);
        check(contains(actual.commands,"No supported installations detected"),"No-installation state has no fabricated usage");
        auto empty_size=usage::ui::hover_size(live,100);
        actual=hover.frame(live,neutral(),empty_size.width,empty_size.height);
        check(contains(actual.commands,"No supported installations detected"),"Combined hover handles no providers");
        live.claude.installed=true;
        live.claude.windows.clear();
        actual=hover.frame(live,neutral(),360,138);
        check(contains(actual.commands,"Connecting..."),"Combined hover handles connecting provider");
        live.claude.error="Offline";
        actual=hover.frame(live,neutral(),360,138);
        check(contains(actual.commands,"Claude - unavailable"),"Combined hover handles unavailable provider");
        live.claude.error.clear();
        live.claude.installed=false;
        live.claude.windows={{"5 hour",97,0},{"Weekly",44,0},{"Fable weekly",23,0}};
        live.account.installed=true;
        actual=details.frame(live,neutral(),400,470);
        check(details.bounds("SessionSlider").width==0,"Live allowance cannot be edited");
        usage::ui::View settings(usage::ui::Surface::Settings,measure,nullptr);
        settings.frame(data,neutral(),400,470);
        auto settings_input = neutral(); settings_input.keyTab = true;
        settings.frame(data,settings_input,400,470);
        settings_input = neutral(); settings_input.keyEnd = true;
        settings.frame(data,settings_input,400,470);
        check(settings.text_percent() == 300,"Settings keyboard selects largest text");
        settings_input = neutral(); settings_input.keyEscape = true;
        check(settings.frame(data,settings_input,400,470).close,"Escape cancels settings");
        const auto save_bounds = settings.bounds("SaveSettings");
        check(save_bounds.height > 0 && save_bounds.y+save_bounds.height <= 470,"Save fits settings window");
        live.claude.installed=true;
        actual=settings.frame(live,neutral(),1120,700);
        check(contains(actual.commands,"Appearance") && contains(actual.commands,"Codex usage") && contains(actual.commands,"Claude usage"),"Unified settings shows appearance and both providers");
        check(contains(actual.commands,"Last error: Connection failed"),"Provider error remains readable");
        for (int i=0; i<actual.commands.length; ++i) {
            const auto& command=actual.commands.internalArray[i];
            if (command.commandType!=CLAY_RENDER_COMMAND_TYPE_TEXT) continue;
            const auto text=command.renderData.text.stringContents;
            const auto value=std::string(text.chars,static_cast<std::size_t>(text.length));
            if (value=="Enable Codex" || value=="Roboto" || value=="Update interval" || value=="1 minute" || value=="Last error: Connection failed" || value=="Plan: Not reported")
                check(command.renderData.text.fontSize==16,"Settings labels, metadata and controls share one body size");
            check(command.renderData.text.fontSize>=14,"Settings text never uses tiny taskbar type");
        }
        const auto providers_scroll=settings.bounds("ProvidersScroll");
        auto provider_wheel=neutral(); provider_wheel.mouseX=providers_scroll.x+30; provider_wheel.mouseY=providers_scroll.y+100;
        settings.frame(live,provider_wheel,1120,700);
        provider_wheel.scrollY=-20;
        actual=settings.frame(live,provider_wheel,1120,700);
        check(contains(actual.commands,"Fable weekly"),"All provider limits remain accessible by scrolling");
        provider_wheel.scrollY=20;
        settings.frame(live,provider_wheel,1120,700);

        check(settings.bounds("RefreshUsage").height>0 && settings.bounds("CodexRefreshUsage").height==0 && settings.bounds("ClaudeClose").height==0,"Unified panel has shared actions");
        check(settings.bounds("SessionSlider").width==0,"Unified live usage is read only");
        const auto toggle=settings.bounds("EnableCodex");
        auto toggle_input=neutral(); toggle_input.mouseX=toggle.x+toggle.width/2; toggle_input.mouseY=toggle.y+toggle.height/2;
        settings.frame(live,toggle_input,1120,700);
        toggle_input.pointerDown=toggle_input.pointerPressed=true;
        settings.frame(live,toggle_input,1120,700);
        toggle_input.pointerDown=toggle_input.pointerPressed=false; toggle_input.pointerReleased=true;
        settings.frame(live,toggle_input,1120,700);
        check(!settings.codex_enabled() && settings.claude_enabled() && live.codex_enabled,"Provider toggles edit a draft until save");
        const auto save_live=settings.bounds("SaveSettings"), refresh_live=settings.bounds("RefreshUsage");
        check(save_live.y+save_live.height<=700 && refresh_live.y+refresh_live.height<=700,"Provider controls and actions fit settings");
        settings.set_providers(true,true);

        const auto refresh=settings.bounds("RefreshUsage");
        auto click=neutral(); click.mouseX=refresh.x+refresh.width/2; click.mouseY=refresh.y+refresh.height/2;
        settings.frame(live,click,1120,700);
        click.pointerDown=click.pointerPressed=true;
        settings.frame(live,click,1120,700);
        click.pointerDown=click.pointerPressed=false; click.pointerReleased=true;
        check(settings.frame(live,click,1120,700).refresh,"Shared refresh requests updated usage");
        check(settings.text_percent()==300,"Usage refresh preserves pending appearance edits");
        live.account.installed=live.claude.installed=false;
        actual=settings.frame(live,neutral(),1120,700);
        check(contains(actual.commands,"Not detected") && settings.bounds("SaveSettings").height>0,"Settings remain available without providers");
        const auto appearance_panel=settings.bounds("AppearancePanel"), provider_panel=settings.bounds("ProvidersPanel");
        check(appearance_panel.x+appearance_panel.width<=provider_panel.x,"Appearance and Providers are distinct panels");
        const auto click_config=[&](const char* name) {
            const auto bounds=settings.bounds(name);
            auto pointer=neutral(); pointer.mouseX=bounds.x+bounds.width/2; pointer.mouseY=bounds.y+bounds.height/2;
            settings.frame(live,pointer,1120,700);
            pointer.pointerDown=pointer.pointerPressed=true;
            settings.frame(live,pointer,1120,700);
            pointer.pointerDown=pointer.pointerPressed=false; pointer.pointerReleased=true;
            return settings.frame(live,pointer,1120,700);
        };
        click_config("FontChoice");
        auto choice_key=neutral(); choice_key.keyDown=true;
        settings.frame(live,choice_key,1120,700);
        choice_key=neutral(); choice_key.keyEnter=true;
        settings.frame(live,choice_key,1120,700);
        check(settings.preferences().appearance.font==1,"Font choice is configurable");
        click_config("ThemeChoice");
        choice_key=neutral(); choice_key.keyEnd=true;
        settings.frame(live,choice_key,1120,700);
        choice_key=neutral(); choice_key.keyEnter=true;
        settings.frame(live,choice_key,1120,700);
        check(settings.preferences().appearance.theme==1,"Theme dropdown updates the appearance");
        const auto background=[&]() {
            auto frame=settings.frame(live,neutral(),1120,700);
            for (int i=0;i<frame.commands.length;++i) {
                const auto& command=frame.commands.internalArray[i];
                if (command.commandType==CLAY_RENDER_COMMAND_TYPE_RECTANGLE && command.id==Clay_GetElementId(CLAY_STRING("SettingsPanel")).id)
                    return command.renderData.rectangle.backgroundColor.r;
            }
            return -1.f;
        };
        check(background()==35,"Slate paints the settings background");
        click_config("ThemeChoice");
        choice_key=neutral(); choice_key.keyHome=true;
        settings.frame(live,choice_key,1120,700);
        choice_key=neutral(); choice_key.keyEnter=true;
        settings.frame(live,choice_key,1120,700);
        check(settings.preferences().appearance.theme==0 && background()==19,"Midnight can be selected again and repaints settings");
        click_config("CodexInterval");
        auto interval_key=neutral(); interval_key.keyEnd=true;
        settings.frame(live,interval_key,1120,700);
        interval_key=neutral(); interval_key.keyEnter=true;
        settings.frame(live,interval_key,1120,700);
        check(settings.preferences().codex_interval==900 && settings.preferences().claude_interval==60,"Provider intervals are independent");
        click_config("ClaudeInterval");
        interval_key=neutral(); interval_key.keyEscape=true;
        check(!settings.frame(live,interval_key,1120,700).close && settings.preferences().claude_interval==60,"Escape dismisses the dropdown without closing Settings");
        const auto footer=settings.bounds("SaveSettings");
        for (const auto* name : {"ResetAppearance","RefreshUsage","CancelSettings"}) {
            const auto button=settings.bounds(name);
            check(button.y==footer.y && button.y+button.height<=700,"All settings actions share one visible row");
        }

        auto scroll_input=neutral(); scroll_input.mouseX=appearance_panel.x+30; scroll_input.mouseY=appearance_panel.y+100;
        settings.frame(live,scroll_input,1120,700);
        const auto delay_before=settings.bounds("HoverDelay");
        scroll_input.scrollY=-20;
        settings.frame(live,scroll_input,1120,700);
        check(settings.bounds("HoverDelay").y<delay_before.y,"Appearance panel scrolls to additional controls");
        click_config("ResetAppearance");
        check(settings.preferences().appearance==usage::Appearance{} && settings.preferences().codex_interval==900,"Reset restores every appearance default without changing providers");
        auto custom=settings.preferences(); custom.appearance.font=2; custom.appearance.show_resets=false;
        widget.set_preferences(custom);
        live.account.installed=true;
        actual=widget.frame(live,neutral(),208,38);
        for (int i=0; i<actual.commands.length; ++i) {
            const auto& command=actual.commands.internalArray[i];
            if (command.commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
                check(command.renderData.text.fontId==2,"Selected font reaches taskbar text");
                const auto value=command.renderData.text.stringContents;
                check(std::string(value.chars,static_cast<std::size_t>(value.length)).find("reset ")==std::string::npos,"Reset label visibility is configurable");
            }
        }
        widget.set_preferences(usage::Preferences{});
        widget.set_text_percent(140);
        auto large = widget.frame(data,neutral(),292,38);
        bool large_text = false;
        for (int i = 0; i < large.commands.length; ++i) {
            const auto& command = large.commands.internalArray[i];
            if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
                large_text = large_text || command.renderData.text.fontSize == 14;
                check(command.boundingBox.y+command.boundingBox.height <= 38,"Large taskbar text fits");
            }
        }
        check(large_text,"Text preference increases rendered font size");
        widget.set_text_percent(100);
        auto overview = hover.frame(data,neutral(),320,250);
        check(contains(overview.commands,"38% used") && contains(overview.commands,"19% used"),"Hover shows used allowances");
        check(contains(overview.commands,"Token counts and reset times unavailable"),"Hover does not invent provider data");
        auto frame = details.frame(data,neutral(),400,470);
        check(contains(frame.commands,"62%") && contains(frame.commands,"81%"),"Details show live values");
        const auto slider = details.bounds("SessionSlider");
        const auto close = details.bounds("Close");
        check(slider.width > 200 && slider.height > 0,"Session slider has usable bounds");
        check(close.y >= 0 && close.y+close.height <= 470,"Close button fits popup");
        auto input = neutral();
        input.mouseX = slider.x+slider.width*0.25f;
        input.mouseY = slider.y+slider.height/2;
        details.frame(data,input,400,470);
        input.pointerDown = input.pointerPressed = true;
        check(details.frame(data,input,400,470).changed && data.session() == 25,"Pointer updates session slider");
        input.pointerPressed = false;
        input.mouseX = slider.x+slider.width+100;
        details.frame(data,input,400,470);
        check(data.session() == 100,"Captured drag clamps outside track");
        input.pointerDown = false; input.pointerReleased = true;
        details.frame(data,input,400,470);
        // Rendering another surface must not steal popup focus/context.
        frame = widget.frame(data,neutral(),208,38);
        check(contains(frame.commands,"100%"),"Taskbar view receives changed usage");
        input = neutral(); input.keyLeft = true;
        details.frame(data,input,400,470);
        check(data.session() == 99,"Keyboard focus survives rendering other view");
        input = neutral(); input.keyTab = true;
        details.frame(data,input,400,470);
        input = neutral(); input.keyHome = true;
        details.frame(data,input,400,470);
        check(data.weekly() == 0,"Tab focuses weekly and Home reaches zero");
        input = neutral(); input.keyEnd = true;
        details.frame(data,input,400,470);
        check(data.weekly() == 100,"End reaches full allowance");
        input = neutral(); input.keyTab = true;
        details.frame(data,input,400,470);
        input = neutral(); input.keyEnter = true;
        check(details.frame(data,input,400,470).close,"Keyboard activates Close");
        details.reset_focus();
        input = neutral(); input.keyEscape = true;
        check(details.frame(data,input,400,470).close,"Escape closes details");
        for (const int value : {0,15,30,100}) {
            data.set_session(value); data.set_weekly(value);
            frame = widget.frame(data,neutral(),208,38);
            check(contains(frame.commands,std::to_string(value)+"%"),"Boundary percentage visible in widget");
            frame = hover.frame(data,neutral(),320,250);
            check(contains(frame.commands,std::to_string(100-value)+"% used"),"Hover updates at allowance boundaries");
            for (int i = 0; i < frame.commands.length; ++i) {
                const auto& command = frame.commands.internalArray[i];
                if (command.commandType == CLAY_RENDER_COMMAND_TYPE_TEXT)
                    check(command.boundingBox.y+command.boundingBox.height <= 250,"Hover text fits card");
            }
        }
        std::cout << "PASS: Clay layouts, pointer drag, focus isolation, keyboard navigation, Close and allowance limits.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
