# Third-party dependencies

| Dependency | How it is included | Revision | License |
| --- | --- | --- | --- |
| [clay-widgets](https://github.com/AlessandroAU/clay-widgets) | Git submodule at `clay-widgets/` | `f382e1b01967e9087f4441feb670803b4a47d830` | MIT |
| [Clay](https://github.com/nicbarker/clay) | clay-widgets' submodule `subprojects/clay` | pinned by clay-widgets (`e6cc369`) | zlib |
| [raylib](https://github.com/raysan5/raylib) | clay-widgets' submodule `subprojects/raylib`, bundled GLFW included | pinned by clay-widgets (`fc03d77`) | zlib; bundled licenses retained in source |
| [FreeType](https://freetype.org) | clay-widgets' submodule `subprojects/freetype` | pinned by clay-widgets (`VER-2-14-3`) | FreeType License (FTL), chosen from its FTL/GPLv2 dual license |
| [nlohmann/json](https://github.com/nlohmann/json) | Vendored: `nlohmann/json.hpp`, `LICENSE.MIT` | `v3.11.3` | MIT |
| [Monocypher](https://monocypher.org) | Vendored: `monocypher/`, the library and its optional Ed25519 files, unmodified from the release tarball (SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`) | `4.0.2` | BSD-2-Clause or CC0-1.0 |
| Roboto Regular | clay-widgets' `assets/generated/embedded-font.h`; the license text is `fonts/LICENSE.txt` | from clay-widgets | Apache 2.0 (Google) |

Clone with `git clone --recursive`, or run `git submodule update --init --recursive` in an existing checkout. CMake stops with that hint when the checkout is missing. Once fetched, builds need no network access.

The app builds everything from the clay-widgets checkout: its split headers, its raylib backend, its tests, and the Clay, raylib and FreeType revisions it pins and tests against. So the widget library and its dependencies cannot drift apart, and nothing here is a modified copy.

## Build integration

`clay_widgets` is a static CMake library. It exports the include directories and C++20 requirement. Link it with:

```cmake
target_link_libraries(your_target PRIVATE clay_widgets)
```

Then include `<clay.h>` and `<clay-widgets/widgets.h>` without defining implementation macros. `src/ui/clay_widgets.cpp` owns both implementations. Third-party warnings are suppressed only for that implementation target, the upstream test target and FreeType; the application's warning-as-error policy remains enabled. The implementation requires **C++20** because of designated initializers; the application's core remains C++17.

`clay_widgets_tests` builds and runs clay-widgets' headless suite from the submodule. `clay_link_test` separately verifies that a consumer can link the static library and generate render commands without implementation macros. Both are registered with CTest alongside the application's tests. Upstream's `tests/test-raylib.cpp` is not built here: it needs a GL window.

Raylib's `CloseWindow` symbol is renamed at compile time to `UsageTrackerRaylibCloseWindow` for raylib and its adapter only, avoiding a user32 symbol collision without changing Windows declarations or upstream source. Raylib audio/models and the high-resolution multimedia timer are disabled in CMake. Roboto is embedded, so its font file is not loaded from disk at runtime.

Text is rasterized with FreeType's light (vertical-only) hinting rather than raylib's unhinted stb_truetype, through the backend's `CLAY_WIDGETS_FREETYPE` option. FreeType is compiled as the `freetype` CMake target with the same source list as clay-widgets' Makefile. The backend's `freetype-config/freetype/config/ftmodule.h` precedes FreeType's headers on that target's include path, so only the compiled modules are registered.

## Updating

Make changes in the clay-widgets repository, run its tests (`make test test-amalgam test-backend`), commit and push. To move raylib or FreeType, update the submodule there first. Then here:

```sh
git -C vendor/clay-widgets fetch
git -C vendor/clay-widgets checkout <commit>
git submodule update --init --recursive
```

Update the revision above, run `build.bat` (it builds with MSVC and runs every CTest suite), and commit the new submodule pointer.

Portions of this software are copyright © The FreeType Project (https://freetype.org). All rights reserved.
