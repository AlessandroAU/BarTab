# Vendored dependencies

These files are part of the source tree. Building does not clone repositories, fetch packages, or require the exploratory checkout in `third_party/`.

| Dependency | Upstream revision | Included files | License |
| --- | --- | --- | --- |
| [Clay](https://github.com/nicbarker/clay) | `e6cc36941ab2af5d81107617039d6f527a1c660b` | `clay/clay.h` (unmodified), `clay/LICENSE.md` | zlib |
| [clay-widgets](https://github.com/AlessandroAU/clay-widgets) | `f6e955be6e5df9c23ac48966233c5caf16a3c176` | Widget headers, headless test suite, MIT license | MIT |
| [raylib](https://github.com/raysan5/raylib) | `caadb48e259028233515777a3e6402040c497309` | Source, bundled GLFW, CMake files and licenses | zlib; bundled licenses retained in source |
| clay-widgets raylib backend | Same clay-widgets revision above | `clay-widgets/backends/raylib/` | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | `v3.11.3` | Unmodified single header `nlohmann/json.hpp` and `LICENSE.MIT` | MIT |
| Roboto Regular | Embedded byte array from the same clay-widgets revision | `fonts/embedded-font.h`, `fonts/LICENSE.txt` | Apache 2.0 (Google) |

The Clay revision matches the clay-widgets repository's pinned submodule. Do not independently upgrade Clay without running the widget tests.

## Local MSVC compatibility changes

The vendored clay-widgets copy is modified. The complete diff against the revisions above is in `patches/clay-widgets-msvc.patch`:

1. Replace C compound literals such as `(Clay_Color){...}` with `CLAY__INIT(Clay_Color) {...}`. Clay's existing macro expands to standard braced initialization in C++ and a compound literal in C. This also applies to the imported tests.
2. Include `<cstddef>` and use `std::max_align_t` in C++ for the widget state pool. This makes the public header self-contained on MSVC instead of depending on an earlier standard-library include. The C branch still uses `max_align_t`.

No widget behavior was intentionally changed. Clay itself is unmodified. The implementation requires **C++20** because of designated initializers; the application's existing core remains C++17.

The raylib backend additionally accepts an explicit scale argument for offscreen scissor rectangles. This is included in the same patch. Raylib's `CloseWindow` symbol is renamed at compile time to `UsageTrackerRaylibCloseWindow` for raylib and its adapter only, avoiding a user32 symbol collision without changing Windows declarations or upstream source. Raylib audio/models and the high-resolution multimedia timer are disabled in CMake. Roboto is embedded, so its font file is not loaded from disk at runtime.

## Build integration

`clay_widgets` is a static CMake library. It exports the include directories and C++20 requirement. Link it with:

```cmake
target_link_libraries(your_target PRIVATE clay_widgets)
```

Then include `<clay.h>` and `<clay-widgets/widgets.h>` without defining implementation macros. `src/ui/clay_widgets.cpp` owns both implementations. Third-party warnings are suppressed only for that implementation target and the imported upstream test target; the application's warning-as-error policy remains enabled.

`clay_widgets_tests` runs the upstream headless suite (566 checks). It has its own implementation instance, following upstream's test harness. `clay_link_test` separately verifies that a consumer can link the static library and generate render commands without implementation macros. Both are registered with CTest alongside the existing core tests.

For an update, replace the included files from pinned upstream revisions, preserve licenses, reapply/review the compatibility diff, and run the full tests on MSVC and Linux GCC. Renderer/font dependencies are separate from the headless widget library; the upstream demo is not included. Raylib/GLFW are built statically with MSVC for the Windows renderer. Their bundled source license notices are preserved.
