# UsageTracker

A Windows 11 taskbar widget built with **C++20, Clay, clay-widgets, and raylib**. Both the transparent taskbar chart and details popup use Clay layouts. Sliders and Close are clay-widgets controls. Normal launches show **live Codex and Claude allowance data**, detected automatically using their existing logins. Demo controls are retained only for the deterministic smoke test.

## Build and run

Requires Visual Studio 2022 with Desktop development with C++, a Windows SDK, and CMake 3.22+. All dependencies and the UI font are vendored; builds need no network access. The executable uses system Windows libraries and an OpenGL 3.3-capable graphics driver. The default Roboto font is embedded. Optional Segoe UI and Consolas choices use the Windows font installation, with Roboto as fallback. The UI needs no .NET or GCC. Live usage requires a separately installed, signed-in Codex and/or Claude executable.

Quit the running app before rebuilding:

```powershell
.\build.bat
.\run.bat
```

The script compiles with MSVC, runs eight test executables, and installs `bin/UsageTracker.exe` and third-party license notices. `run.bat` stops existing instances and starts the installed build.

The MSVC runtime and project/third-party libraries are statically linked, so no Visual C++ redistributable or companion library DLLs are required. Windows system DLLs and the graphics driver remain runtime dependencies. The application icon is embedded in the EXE, including multiple sizes for Explorer, windows, and the notification tray; source artwork and generation notes are in `assets/`.

Equivalent CMake commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix bin
```

## Interaction

- Click the widget or tray icon, or choose **Settings and usage...**, to open one window with distinct **Appearance** and **Providers** panels. Each panel scrolls independently; Save, Cancel, Refresh, and Reset share one fixed bottom row. Settings fit the monitor's work area.
- **Providers** always lists Codex and Claude, including disabled or missing installations. Each card includes detection status, executable path, plan, all reported allowance windows and reset times, the full last error, and the last successful update time (or Never). Enable or disable each provider and choose independent update intervals from dropdowns: 15 or 30 seconds, or 1, 2, 5, 10, or 15 minutes (1 minute by default). Existing custom intervals remain selected until changed. Disabled providers stop polling; their retained readings and detected installations remain inspectable. **Refresh usage and detection** rechecks installations and refreshes enabled providers.
- **Appearance** offers Roboto/Segoe UI/Consolas font and Midnight/Slate theme dropdowns, 100-300% text size (150% by default), teal/blue/purple accents, widget width (100-400 px, 150 px by default), bar thickness (3-9 px), corner radius (0-12 px), taskbar reset-label visibility, hover enable/disable, hover width (240-600 px), opacity (50-100%), and delay (100-1500 ms). Dimensions are logical pixels before text/DPI scaling. **Reset appearance to defaults** restores all appearance choices without changing providers or intervals.
- Changes preview immediately. Hover the widget while Settings is open to preview the card. **Save settings** persists both panels to `%LOCALAPPDATA%/UsageTracker/settings.ini`; Cancel, Escape, or closing restores the previous preferences. Existing settings files keep their saved text size and provider choices; new options get defaults. Above 160%, taskbar rows sit side by side to keep large text within the taskbar height. Larger text or widget widths need more taskbar space; the tray remains available if no gap fits.
- Hover for 350 ms by default to see one compact, subtly translucent card with Codex and Claude stacked together. Each allowance shows its remaining percentage and local reset time; stale and unavailable providers are marked. The card grows with the number of returned limits and respects the text-size setting. The taskbar shows Codex's lowest remaining allowance. When both providers are active and Claude reports Weekly and Fable weekly, its row uses two thin tracks: teal overall weekly above amber Fable weekly, with percentages and reset times in the same order (`44 / 23%`). With only Claude active, **General** and **Fable** occupy separate taskbar rows. Single allowance rows show local reset times as `DD/MM HH:MM`; the paired Claude row shows both reset dates as `DD/MM` to fit the existing taskbar width. Widths below 208 px use shorter taskbar reset dates and compact paired labels. Exact times remain in the hover card. `?` means unavailable. Otherwise Claude falls back to its lowest remaining allowance; hover/details list every returned window, including Claude model-specific limits. The card never takes focus and dismisses on leave or click.
- Click anywhere in the taskbar widget rectangle, including transparent gaps, to open the unified settings and usage panel.
- The panel has a single **Refresh usage** action for enabled providers; use Settings to access it. Live allowance values cannot be edited.
- Tab / Shift+Tab moves focus between controls. Arrows adjust the settings slider; Enter activates buttons; Escape closes the popup.
- Right-click the widget or tray icon for **Settings**, **Start at boot**, and **Quit**. Start at boot is a checked toggle that adds/removes the current quoted executable path in `HKCU/Software/Microsoft/Windows/CurrentVersion/Run` under `UsageTracker`. It launches at user sign-in without administrator rights. If you move the EXE, toggle it off and on from the new location. The tray icon remains available when taskbar space is unavailable.
- Startup shows a loading state. A failed refresh marks retained readings as stale; an initial failure shows unavailable instead of invented percentages.

## Architecture

| Location | Responsibility |
| --- | --- |
| `src/core/codex.*` | Portable Codex response parsing and duration-based window labels. |
| `src/core/claude.*` | Claude usage parsing, model windows, and UTC reset timestamps. |
| `src/windows/providers.*` | Installation detection, managed CLI processes, both protocols, and independent refresh workers. |
| `src/core/preferences.hpp` | Shared appearance/provider defaults, validation, and snapshot comparisons. |
| `src/core/usage.*` | Portable usage state, warning thresholds, and free-space algorithm (C++17). |
| `src/ui/views.*` | Portable Clay layouts, clay-widgets controls, independent focus/layout contexts (C++20). |
| `src/ui/raylib_renderer.*` | Clay commands to offscreen textures, font cache, premultiplied BGRA output. |
| `src/windows/main.cpp` | Process entry point and single-instance guard. |
| `src/windows/app.*` | Application lifetime, shared state, controller messages, and tray/menu integration. |
| `src/windows/widget.cpp` | Taskbar placement, transparent chart presentation, and hover card. |
| `src/windows/popup.cpp` | Details/settings hosting, input translation, and bitmap presentation. |
| `src/windows/settings.cpp` | Preference loading, saving, and application to views. |
| `src/windows/platform.*` | Executable path and diagnostic logging. |
| `src/windows/smoke_test.cpp` | Live desktop checks and diagnostic screenshots. |
| `src/windows/taskbar.*` | Explorer discovery and UI Automation snapshots on a worker thread. |
| `vendor/` | Pinned dependencies, embedded font, licenses, compatibility patch. |
| `tests/` | Core, application UI interaction, and library linkage tests. |

Raylib owns one hidden OpenGL context. It draws Clay commands into cached offscreen textures; Windows presents the pixels in the existing native windows. Text and shapes are rendered by raylib. GDI is used only to present bitmap pixels. The tray, context menu, title bar, and Explorer integration remain native OS surfaces.

The taskbar bitmap uses minimal background alpha (1/255), so it looks transparent while retaining a full rectangular click target. The layout worker finds unoccupied taskbar space. The widget hides if no gap fits or layout information becomes stale, and is recreated if its parent disappears.

### Rendering and idle behavior

There is **no game loop or continuous rendering**. Animations are disabled. Input, value changes, DPI/geometry changes, and necessary paints drive updates; popup paints reuse cached pixels. An unchanged placement tick does not redraw. Raylib's high-resolution multimedia timer is disabled. The hidden popup has no render timer.

Offscreen presentation copies pixels from GPU to CPU when a view redraws. This is an integration tradeoff for these small surfaces, not a claim that it uses less battery than GDI. The GPU context remains allocated, and taskbar discovery still polls. Battery impact needs measurement on the target laptop.

## Linux portability

The model, Clay layouts, interaction logic, and headless tests build on Linux. **A Linux desktop/panel host is not implemented.** The Windows renderer and presentation are exercised by the Windows app today.

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
```

This builds portable libraries and six test executables without a graphical display. Future Linux work can reuse the views with raylib and implement desktop-specific hosting and input.

## Verification

CTest covers placement, allowance limits, Clay layouts, pointer dragging beyond sliders, independent view focus, keyboard navigation, Close, library linkage, and the upstream clay-widgets suite (418 checks). Checks remain active in Release builds.

With the desktop unlocked and no other instance running:

```powershell
$smoke = Start-Process .\bin\UsageTracker.exe -ArgumentList '--smoke-test' -Wait -PassThru
$smoke.ExitCode
Get-Content .\bin\smoke-test.txt
```

The smoke test checks live taskbar parenting, rectangular hit detection, native mouse messages routed into Clay sliders, keyboard updates, Close, child recreation, unchanged-tick redraw counts, visible bar pixels, hover dismissal, settings centering/save/cancel, live previews, distinct panels, font and interval changes, appearance reset, cancellation of both panels, persistence, and retained detection for disabled providers. Preference checks use a separate `smoke-settings.ini` to preserve user preferences. It saves `widget-live.bmp`, `details-live.bmp`, `hover-live.bmp`, `settings-live.bmp`, `unified-settings.bmp`, `combined-hover.bmp`, and `claude-only-taskbar.bmp` beside the executable and exits after about eight seconds. It does not restart Explorer or change Windows settings. Exit codes: 0 success, 1 failure, 2 another instance running.

Logs: `%LOCALAPPDATA%/UsageTracker/prototype.log`. Tested on Windows 11 build 26200 at 100% scaling with MSVC 19.44 and Ubuntu 22.04 WSL with GCC 11.4 (portable/headless tests).

## Remaining scope

Additional service connections, startup installation, Linux hosting, and accessibility-provider integration remain future work. Primary horizontal Windows taskbar only; there is no reserved taskbar space, so newly appearing buttons may overlap briefly between snapshots. Auto-hide, actual Explorer restart, multi-monitor/DPI transitions, and laptop power use still need manual validation.

See [vendor/README.md](vendor/README.md) for revisions, licenses, and local adaptations. The independently implemented taskbar embedding technique was informed by [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).

## Development conventions

Keep portable state in `src/core`, Clay layouts in `src/ui`, and operating-system integration in `src/windows`. The application owns its renderer and views for their entire lifetime; the renderer must outlive the views. Changes to placement, input, or settings should also pass the live smoke test.

`.editorconfig` defines source whitespace and encoding. `vendor/` contains the pinned build dependencies; document intentional upstream changes in `vendor/README.md`. The ignored `third_party/` directory is an exploratory checkout, not a build dependency. Generated binaries, screenshots, and build directories are ignored.

## Service connections

Install and sign in to Codex CLI/the Codex VS Code extension, Claude Code, or both. Detection checks explicit `USAGETRACKER_CODEX` / `USAGETRACKER_CLAUDE` overrides first, then executable files on PATH, `.local/bin`, `.cargo/bin`, Scoop shims/current packages, WinGet Links, and native binaries within npm packages (including custom npm prefixes and Node/Volta locations). It also searches recognized OpenAI/Anthropic extensions in VS Code, Insiders, VS Code OSS, Cursor, and Windsurf folders, plus `VSCODE_EXTENSIONS`, `VSCODE_EXTENSIONS_DIR`, `VSCODE_PORTABLE`, and portable editor folders next to PATH bin directories. The newest available extension binary is preferred. Searches are bounded, skip inaccessible folders, and avoid ARM64 bundles in this x64 build. Only native `.exe` helpers are launched, not shell scripts.

An invalid override shows an actionable error rather than silently selecting another installation. Missing providers show **Not detected** in Settings, disappear from usage displays, and do not stop the other provider. Detection repeats at each enabled provider's configured interval, when Settings opens, and on manual refresh, so a newly installed provider can recover without restarting the app. The widget lets each CLI handle authentication without reading or storing credentials itself.

At the configured interval (60 seconds by default, measured after each completed request), a background worker starts a hidden `codex app-server --listen stdio://`, completes the initialize/initialized handshake, calls `account/rateLimits/read`, and closes its managed process tree. Requests time out after 20 seconds; UI rendering stays event-driven. Closing the widget cancels pending work. Claude has an independent worker and uses the headless `initialize` / `get_usage` control protocol with safe mode, no session persistence, and no model prompt. Each worker has separate data and errors. Both CLI tools must be installed separately; builds remain offline. Claude's control protocol was verified with version 2.1.269 and may change in future versions.

The named `codex` bucket is preferred, with the legacy single-bucket response as fallback. Primary/secondary windows are optional and labels come from their duration. Other model buckets are not displayed yet. Claude prefers its normalized limit list, including model-scoped weekly limits, and falls back to legacy usage fields. Claude's paired weekly tracks share the existing row; all other windows remain in hover/details. Remaining percentage is computed from used percentage; it is not an exact token budget. Missing reset times are displayed as unavailable. Last-good values are retained in memory only.

`bin/UsageTracker.exe --live-smoke-test` checks both detected services and captures the widget, hover, and details windows. Results are written to `bin/codex-live-test.txt`; this mode requires signed-in detected installations and network access. `--smoke-test` remains offline with isolated demo data and settings.
