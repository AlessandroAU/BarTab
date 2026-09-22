# UsageTracker

A Windows 11 taskbar widget that shows how much **Codex** and **Claude** allowance you have left.

It reads usage from the Codex and Claude CLIs you already have installed and signed in, and draws a transparent chart directly in the taskbar. Built with C++20, [Clay](https://github.com/nicbarker/clay), [clay-widgets](https://github.com/AlessandroAU/clay-widgets) and raylib.

![The widget embedded in the taskbar](docs/images/taskbar-widget.png)

Hover for a compact card with every reported allowance window and its local reset time:

![The hover card](docs/images/hover-card.png)

Click the widget, or the tray icon, for settings and full usage detail:

![The settings and usage panel](docs/images/settings.png)

## Build and run

Requires Visual Studio 2022 (Desktop development with C++), a Windows SDK, and CMake 3.22+. Every dependency and the UI font is vendored, so builds need no network access. At runtime you need Windows system DLLs and an OpenGL 3.3-capable driver — no .NET and no Visual C++ redistributable, since the MSVC runtime and all libraries are linked statically.

```powershell
.\build.bat   # configure, compile, run the tests, install to bin\
.\run.bat     # start the installed build
```

Both scripts stop any running instance first. Windows Release builds optimize for size, enable link-time optimization across the app and static libraries, remove unused code/data, fold identical sections, and omit debug symbols. Debug builds retain their normal debugging settings. LTO can be disabled when configuring with `-DUSAGETRACKER_ENABLE_LTO=OFF`.

Equivalent CMake commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix bin
```

Live usage needs a separately installed, signed-in Codex and/or Claude CLI.

## Using it

- **Click** the widget — anywhere in its rectangle, including the transparent gaps — or the tray icon to open the settings and usage panel.
- **Hover** it for the usage card. The card never takes focus and closes on leave or click.
- **Right-click** the widget or tray icon for Settings, **Start at boot**, and Quit. Start at boot registers the current executable path under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; move the EXE and you need to toggle it off and on again.
- **Keyboard**: Tab / Shift+Tab moves focus, arrows adjust sliders, Enter activates, Escape closes.

Settings has two independently scrolling panels — **Appearance** and **Providers** — over one fixed button row. Changes preview live; **Save settings** writes `%LOCALAPPDATA%\UsageTracker\settings.ini`, and Cancel, Escape or closing the window restores what was saved.

### Appearance

| Setting | Range | Default |
| --- | --- | --- |
| Text size | 100–300% | 150% |
| Font | Roboto, Segoe UI, Consolas | Roboto |
| Accent | Teal, Blue, Purple | Teal |
| Theme | Midnight, Slate | Midnight |
| Widget width | 100–400 px | 150 px |
| Taskbar position | 0–100% from left | 100% |
| Bar thickness | 3–9 px | 7 px |
| Corner radius | 0–12 px | 10 px |
| Hover card | on / off, 240–600 px wide | on, 360 px |
| Hover opacity | 50–100% | 95% |
| Hover delay | 50–1500 ms | 50 ms |

Dimensions are logical pixels, before text and DPI scaling. Above 160% text, the taskbar rows sit side by side so large text still fits the taskbar height. **Reset appearance to defaults** leaves provider settings alone.

**Taskbar position** slides the widget along the taskbar: 0% is hard left, 50% centred, 100% hard right. The slider names the spot you want, and the widget takes the closest one it can actually reach — it lands in whichever run of free space gets nearest, so it can sit up against a taskbar button but never underneath one. Free space is recomputed as buttons come and go, so this is a preference rather than a fixed coordinate: on a busy taskbar only one gap may be wide enough, and every position resolves to it.

### Providers

Codex and Claude are always listed, including ones that are disabled or not installed. Each card shows detection status, executable path, plan, every reported allowance window with its reset time, the last successful update, and the full text of the last error.

Each provider has its own enable toggle and update interval (15 s, 30 s, or 1, 2, 5, 10, 15 minutes; 1 minute by default). Disabling one stops its polling but keeps its last readings inspectable. **Refresh usage and detection** rechecks installations and refreshes enabled providers immediately.

### What the taskbar row shows

The taskbar has room for less than the hover card, so it summarises. Codex shows its lowest remaining allowance. When both providers are active and Claude reports both Weekly and Fable weekly, Claude's row splits into two thin tracks — overall weekly above Fable weekly — with percentages and reset dates in the same order (`44 / 23%`). With only Claude enabled, **General** and **Fable** get separate rows:

![The taskbar row with only Claude enabled](docs/images/taskbar-widget-claude.png)

Reset times appear as `DD/MM HH:MM`, shortened to `DD/MM` on the paired row and at widths under 208 px; exact times stay in the hover card. `?` means unavailable. A failed refresh marks retained readings stale rather than inventing numbers.

## How it works

Raylib owns a single hidden OpenGL context and draws Clay commands into cached offscreen textures; Windows then presents those pixels in native windows. GDI only blits. The tray, context menu and title bar stay native OS surfaces.

There is **no game loop**. Animations are off, and redraws happen only on input, value changes, DPI or geometry changes. An unchanged placement tick does not repaint, and the hidden popup has no timer. The tradeoff is that each redraw copies pixels from GPU to CPU; for surfaces this small that is an integration choice, not a measured battery win.

The taskbar bitmap uses a background alpha of 1/255, so it looks transparent but keeps a full rectangular click target. A worker thread finds unoccupied taskbar space via UI Automation; the widget hides when no gap fits and is recreated if its parent disappears. There is no reserved taskbar space, so a newly appearing button can overlap briefly between snapshots.

### Layout

| Location | Responsibility |
| --- | --- |
| `src/core/` | Portable usage state, Codex/Claude parsing, preferences, free-space algorithm. |
| `src/ui/views.*` | Portable Clay layouts and clay-widgets controls, one focus context per surface. |
| `src/ui/raylib_renderer.*` | Clay commands to offscreen textures, font cache, premultiplied BGRA output. |
| `src/windows/` | Entry point, app lifetime, taskbar placement, popup hosting, settings I/O, detection, tray. |
| `src/tools/screenshots.cpp` | Renders the README images offscreen from fixed data. |
| `vendor/` | Pinned dependencies, embedded font, licenses, compatibility patch. |
| `tests/` | Core, UI interaction, and library linkage tests. |

Keep portable state in `src/core`, layouts in `src/ui`, and OS integration in `src/windows`. The application owns its renderer and views for their whole lifetime, and the renderer must outlive the views.

## Providers and detection

Detection checks the `USAGETRACKER_CODEX` / `USAGETRACKER_CLAUDE` overrides first, then PATH, `.local/bin`, `.cargo/bin`, Scoop, WinGet, and native binaries inside npm packages (including custom prefixes and Node/Volta locations). It also searches OpenAI/Anthropic extensions in VS Code, Insiders, VS Code OSS, Cursor and Windsurf, plus the `VSCODE_EXTENSIONS*` and `VSCODE_PORTABLE` locations. Searches are bounded, skip inaccessible folders, and ignore ARM64 bundles in this x64 build. Only native `.exe` helpers are launched. An invalid override reports an error rather than silently picking something else.

Detection repeats at each provider's interval, when Settings opens, and on manual refresh, so installing a provider recovers without a restart.

For usage, a background worker starts a hidden `codex app-server --listen stdio://`, completes the initialize handshake, calls `account/rateLimits/read`, and closes the managed process tree. Claude has an independent worker using the headless `initialize` / `get_usage` control protocol in safe mode, with no session persistence and no model prompt. Requests time out after 20 seconds. The widget never reads or stores credentials — each CLI handles its own authentication.

Remaining percentage is derived from used percentage; it is not an exact token budget. Missing reset times show as unavailable, and last-good values are kept in memory only. Claude's control protocol was verified against version 2.1.269 and may change.

## Testing

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Eight suites cover placement, allowance limits, Clay layouts, pointer dragging, focus, keyboard navigation, library linkage, and the upstream clay-widgets suite (566 checks). Checks stay active in Release builds.

A live desktop smoke test exercises the real taskbar, hit detection, native mouse messages routed into Clay, and settings save/cancel. Run it unlocked with no other instance:

```powershell
$smoke = Start-Process .\bin\UsageTracker.exe -ArgumentList '--smoke-test' -Wait -PassThru
$smoke.ExitCode   # 0 success, 1 failure, 2 another instance running
Get-Content .\bin\smoke-test.txt
```

It uses an isolated `smoke-settings.ini`, stays offline, writes diagnostic bitmaps beside the executable, and exits after about eight seconds. It does not restart Explorer or change Windows settings. `--live-smoke-test` is the online variant against real signed-in providers, reporting to `bin/codex-live-test.txt`.

Logs: `%LOCALAPPDATA%\UsageTracker\prototype.log`. Tested on Windows 11 build 26200 at 100% scaling with MSVC 19.44, and on Ubuntu 22.04 (WSL, GCC 11.4) for the portable tests.

### Regenerating the screenshots

The README images are rendered offscreen from fixed demo data — no taskbar, no visible window, no provider processes — so they stay in sync with the UI:

```powershell
cmake --build build --config Release --target docs-screenshots
```

## Linux

The model, Clay layouts, interaction logic and headless tests build on Linux; **a Linux desktop host is not implemented**.

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
```

## Remaining scope

Additional service connections, startup installation, Linux hosting, and accessibility-provider integration are future work. Primary horizontal taskbar only. Auto-hide, Explorer restart, multi-monitor and DPI transitions, and laptop power use still need manual validation.

See [vendor/README.md](vendor/README.md) for pinned revisions, licenses and local adaptations. The independently implemented taskbar embedding technique was informed by [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).
