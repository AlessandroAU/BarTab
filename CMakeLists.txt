cmake_minimum_required(VERSION 3.22)
project(UsageTracker VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(USAGETRACKER_ENABLE_LTO "Enable link-time optimization in Release and MinSizeRel" ON)
if(USAGETRACKER_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(LANGUAGES C CXX)
    # Set before creating targets so static dependencies also participate.
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Size optimization and individually discardable functions/global data,
    # including vendored C libraries that reset directory compile options.
    foreach(language C CXX)
        foreach(configuration RELEASE MINSIZEREL)
            string(REGEX REPLACE "/O[12]" "/O1"
                CMAKE_${language}_FLAGS_${configuration} "${CMAKE_${language}_FLAGS_${configuration}}")
            string(APPEND CMAKE_${language}_FLAGS_${configuration} " /Gy /Gw")
        endforeach()
    endforeach()
    # MSVC emits debug symbols separately; omit release debug records and PDBs.
    add_link_options("$<$<CONFIG:Release,MinSizeRel>:/OPT:REF;/OPT:ICF;/INCREMENTAL:NO;/DEBUG:NONE>")
    if(USAGETRACKER_ENABLE_LTO)
        # Full link-time code generation, never MSBuild's incremental default.
        # MSVC 19.41's /LTCG:incremental reused stale code for callers of a
        # changed inline function: after clay-widgets' TabEx changed, only 4 of
        # 829 functions were regenerated and a widget test failed, while a clean
        # build of the same sources passed.
        add_link_options("$<$<CONFIG:Release,MinSizeRel>:/LTCG>")
    endif()
    add_compile_options(/W4 /WX /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Layers, each depending only on those above it:
#   usage_core      portable state, provider protocols and parsers, preferences
#   usage_ui        Clay layouts and pixel effects (no GPU, no OS)
#   usage_renderer  raylib + FreeType: layouts to premultiplied pixels
#   usage_host      services every desktop host shares (provider polling,
#                   CLI discovery, settings file, process transport), with the
#                   per-OS parts from src/windows, src/posix, src/linux, src/macos
#   the app         one native host per desktop: src/windows (Win32) and
#                   src/linux (X11, including XWayland)

add_library(usage_core STATIC src/core/usage.cpp src/core/codex.cpp src/core/claude.cpp src/core/provider_protocol.cpp
    src/core/mock_provider.cpp)
target_include_directories(usage_core SYSTEM PRIVATE vendor/nlohmann)
if(MSVC)
    target_compile_definitions(usage_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_include_directories(usage_core PUBLIC src)

# clay-widgets is a git submodule, and it pins the Clay, raylib and FreeType
# revisions it is tested against as its own submodules; everything builds from
# those, so the widget library and its dependencies can never disagree.
set(CLAY_WIDGETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/clay-widgets")
foreach(required clay-widgets/widgets.h subprojects/clay/clay.h subprojects/raylib/CMakeLists.txt
                 subprojects/freetype/src/base/ftbase.c)
    if(NOT EXISTS "${CLAY_WIDGETS_DIR}/${required}")
        message(FATAL_ERROR "vendor/clay-widgets and its submodules are not checked out. Run: git submodule update --init --recursive")
    endif()
endforeach()
add_library(clay_widgets STATIC src/ui/clay_widgets.cpp)
target_include_directories(clay_widgets SYSTEM PUBLIC "${CLAY_WIDGETS_DIR}" "${CLAY_WIDGETS_DIR}/subprojects/clay")
target_compile_features(clay_widgets PUBLIC cxx_std_20)
if(MSVC)
    # Suppress upstream warnings only in the third-party implementation TU.
    target_compile_options(clay_widgets PRIVATE /W0 /WX-)
    target_compile_definitions(clay_widgets PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(clay_widgets PRIVATE -w)
endif()

add_library(usage_ui STATIC src/ui/views.cpp src/ui/confetti.cpp src/ui/pixels.cpp)
target_include_directories(usage_ui PUBLIC src)
target_link_libraries(usage_ui PUBLIC usage_core clay_widgets)

# ---- Renderer: raylib owns one hidden OpenGL context on every platform ----
if(MSVC)
    set(vendored_c_quiet /W0 /WX-)
else()
    set(vendored_c_quiet -w)
endif()
# Keep vendored C warnings separate from the application's strict C++ policy.
function(add_raylib)
    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RMODELS OFF CACHE BOOL "" FORCE)
    set(SUPPORT_WINMM_HIGHRES_TIMER OFF CACHE BOOL "" FORCE)
    # The Linux host draws through X11 windows (XWayland on Wayland desktops),
    # so GLFW's context must be an X11 one too.
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    add_subdirectory("${CLAY_WIDGETS_DIR}/subprojects/raylib" "${CMAKE_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)
endfunction()
get_directory_property(app_compile_options COMPILE_OPTIONS)
add_raylib()
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${app_compile_options}")
# Hinted glyph rasterization for the raylib backend. Only the modules the
# backend's freetype-config/ftmodule.h registers are compiled; that directory
# precedes FreeType's include/ so its module list replaces the default one.
set(freetype_dir "${CLAY_WIDGETS_DIR}/subprojects/freetype")
add_library(freetype STATIC
    "${freetype_dir}/src/base/ftsystem.c"
    "${freetype_dir}/src/base/ftinit.c"
    "${freetype_dir}/src/base/ftdebug.c"
    "${freetype_dir}/src/base/ftbase.c"
    "${freetype_dir}/src/base/ftbitmap.c"
    "${freetype_dir}/src/base/ftmm.c"
    "${freetype_dir}/src/autofit/autofit.c"
    "${freetype_dir}/src/truetype/truetype.c"
    "${freetype_dir}/src/cff/cff.c"
    "${freetype_dir}/src/sfnt/sfnt.c"
    "${freetype_dir}/src/psaux/psaux.c"
    "${freetype_dir}/src/psnames/psnames.c"
    "${freetype_dir}/src/pshinter/pshinter.c"
    "${freetype_dir}/src/smooth/smooth.c"
    "${freetype_dir}/src/gzip/ftgzip.c"
)
target_include_directories(freetype BEFORE PRIVATE "${CLAY_WIDGETS_DIR}/backends/raylib/freetype-config")
target_include_directories(freetype SYSTEM PUBLIC "${freetype_dir}/include")
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY _CRT_SECURE_NO_WARNINGS)
target_compile_options(freetype PRIVATE ${vendored_c_quiet})
if(NOT MSVC)
    # GCC repeats FreeType's warnings at link time under LTO, where -w no
    # longer applies; they are upstream's, so keep FreeType out of LTO there.
    set_target_properties(freetype PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
                                              INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF)
endif()
add_library(usage_renderer STATIC src/ui/raylib_renderer.cpp)
target_compile_definitions(usage_renderer PRIVATE CLAY_WIDGETS_FREETYPE)
if(WIN32)
    # raylib's C API name collides with user32's CloseWindow in a native host.
    target_compile_definitions(raylib PRIVATE CloseWindow=UsageTrackerRaylibCloseWindow)
    target_compile_definitions(usage_renderer PRIVATE CloseWindow=UsageTrackerRaylibCloseWindow)
endif()
target_include_directories(usage_renderer SYSTEM PRIVATE "${CLAY_WIDGETS_DIR}")
target_link_libraries(usage_renderer PUBLIC usage_ui PRIVATE raylib freetype)

# ---- Host services shared by every desktop host ----
add_library(usage_host STATIC
    src/host/log.cpp
    src/host/discovery.cpp
    src/host/service_discovery.cpp
    src/host/settings_store.cpp
    src/host/providers.cpp
)
target_link_libraries(usage_host PUBLIC usage_core)
if(WIN32)
    target_sources(usage_host PRIVATE
        src/windows/platform.cpp
        src/windows/process_transport.cpp
        src/windows/locations.cpp
        src/windows/startup.cpp
    )
    target_compile_definitions(usage_host PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(usage_host PUBLIC user32 gdi32 dwmapi advapi32)
else()
    target_sources(usage_host PRIVATE src/posix/process_transport.cpp src/posix/locations.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(usage_host PUBLIC Threads::Threads)
    if(APPLE)
        target_sources(usage_host PRIVATE src/macos/platform.cpp src/macos/startup.cpp)
    else()
        target_sources(usage_host PRIVATE src/linux/platform.cpp src/linux/startup.cpp)
        find_package(Fontconfig REQUIRED)
        target_link_libraries(usage_host PRIVATE Fontconfig::Fontconfig)
        # The desktop portal and GSettings report dark mode, the accent color,
        # reduced motion and the UI font. Without GIO the host still runs, with
        # a dark theme, the default accent and fontconfig's sans-serif.
        find_package(PkgConfig)
        if(PkgConfig_FOUND)
            pkg_check_modules(GIO IMPORTED_TARGET gio-2.0)
        endif()
        if(GIO_FOUND)
            target_link_libraries(usage_host PRIVATE PkgConfig::GIO)
            target_compile_definitions(usage_host PRIVATE USAGETRACKER_HAVE_GIO)
        else()
            message(STATUS "GIO not found: the Linux host will not follow the desktop's theme, accent or font.")
        endif()
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(provider_protocol_tests tests/provider_protocol_tests.cpp)
    target_link_libraries(provider_protocol_tests PRIVATE usage_core)
    target_include_directories(provider_protocol_tests SYSTEM PRIVATE vendor/nlohmann)
    add_test(NAME provider_protocol COMMAND provider_protocol_tests)
    add_executable(settings_edit_tests tests/settings_edit_tests.cpp)
    target_link_libraries(settings_edit_tests PRIVATE usage_core)
    add_test(NAME settings_edit COMMAND settings_edit_tests)
    add_executable(claude_tests tests/claude_tests.cpp)
    target_link_libraries(claude_tests PRIVATE usage_core)
    add_test(NAME claude_limits COMMAND claude_tests)
    add_executable(codex_tests tests/codex_tests.cpp)
    target_link_libraries(codex_tests PRIVATE usage_core)
    add_test(NAME codex_limits COMMAND codex_tests)
    add_executable(usage_ui_tests tests/ui_tests.cpp)
    target_link_libraries(usage_ui_tests PRIVATE usage_ui)
    add_test(NAME usage_ui COMMAND usage_ui_tests)
    add_executable(clay_link_test tests/clay_link_test.cpp)
    target_link_libraries(clay_link_test PRIVATE clay_widgets)
    add_test(NAME clay_link COMMAND clay_link_test)
    add_executable(clay_widgets_tests "${CLAY_WIDGETS_DIR}/tests/test-widgets.cpp")
    target_include_directories(clay_widgets_tests PRIVATE "${CLAY_WIDGETS_DIR}" "${CLAY_WIDGETS_DIR}/subprojects/clay")
    set_target_properties(clay_widgets_tests PROPERTIES CXX_STANDARD 20)
    if(MSVC)
        target_compile_options(clay_widgets_tests PRIVATE /W0 /WX-)
        target_compile_definitions(clay_widgets_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(clay_widgets_tests PRIVATE -w)
    endif()
    add_test(NAME clay_widgets COMMAND clay_widgets_tests)
    add_executable(mock_provider_tests tests/mock_provider_tests.cpp)
    target_link_libraries(mock_provider_tests PRIVATE usage_ui)
    add_test(NAME mock_providers COMMAND mock_provider_tests)
    add_executable(usage_core_tests tests/core_tests.cpp)
    target_link_libraries(usage_core_tests PRIVATE usage_core)
    add_test(NAME usage_core COMMAND usage_core_tests)
    # Host services, on every platform.
    add_executable(process_transport_tests tests/process_transport_tests.cpp)
    target_link_libraries(process_transport_tests PRIVATE usage_host)
    add_test(NAME process_transport COMMAND process_transport_tests)
    set_tests_properties(process_transport PROPERTIES TIMEOUT 15)
    add_executable(settings_store_tests tests/settings_store_tests.cpp)
    target_link_libraries(settings_store_tests PRIVATE usage_host)
    add_test(NAME settings_store COMMAND settings_store_tests)
    add_executable(discovery_tests tests/discovery_tests.cpp)
    target_link_libraries(discovery_tests PRIVATE usage_host)
    add_test(NAME executable_discovery COMMAND discovery_tests)
    add_executable(host_tests tests/host_tests.cpp)
    target_link_libraries(host_tests PRIVATE usage_host usage_ui)
    add_test(NAME host_services COMMAND host_tests)
    set_tests_properties(host_services PROPERTIES TIMEOUT 15)
    add_executable(startup_tests tests/startup_tests.cpp)
    target_link_libraries(startup_tests PRIVATE usage_host)
    add_test(NAME startup_toggle COMMAND startup_tests)
endif()

# Regenerates the README images offscreen. Not part of the test suite: it needs
# a GPU context and writes into the source tree.
add_executable(screenshots src/tools/screenshots.cpp)
target_link_libraries(screenshots PRIVATE usage_renderer usage_host)
add_custom_target(docs-screenshots
    COMMAND screenshots "${CMAKE_CURRENT_SOURCE_DIR}/docs/images"
    DEPENDS screenshots
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Rendering README screenshots")

# ---- The desktop app: one native host per platform ----
set(app_targets)
if(WIN32)
    enable_language(RC)
    if(NOT MSVC)
        message(FATAL_ERROR "The Windows host requires MSVC and the Windows SDK.")
    endif()
    # Everything but the entry point, shared by the app and its debug build.
    add_library(usage_windows OBJECT
        src/windows/app.cpp
        src/windows/settings.cpp
        src/windows/widget.cpp
        src/windows/frame_clock.cpp
        src/windows/popup.cpp
        src/windows/menu.cpp
        src/windows/smoke_test.cpp
        src/windows/taskbar.cpp
    )
    target_compile_definitions(usage_windows PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_include_directories(usage_windows PUBLIC assets)
    target_link_libraries(usage_windows PUBLIC usage_renderer usage_host shell32 ole32 oleaut32 uiautomationcore)
    add_executable(UsageTracker WIN32 src/windows/resources.rc src/windows/main.cpp)
    target_link_libraries(UsageTracker PRIVATE usage_windows)
    target_link_options(UsageTracker PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
    # The same app with mock provider endpoints and a control panel to set
    # them, for trying every provider combination without real accounts.
    add_executable(UsageTrackerDebug WIN32 src/windows/resources.rc src/windows/main.cpp src/windows/mock_panel.cpp)
    target_compile_definitions(UsageTrackerDebug PRIVATE USAGETRACKER_MOCK)
    target_link_libraries(UsageTrackerDebug PRIVATE usage_windows comctl32)
    target_link_options(UsageTrackerDebug PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
    list(APPEND app_targets UsageTracker UsageTrackerDebug)
elseif(APPLE)
    message(STATUS "macOS: building the portable libraries, host services and tests. The menu-bar host is not implemented yet.")
else()
    find_package(X11 REQUIRED)
    if(NOT TARGET X11::Xext OR NOT TARGET X11::Xrandr)
        message(FATAL_ERROR "The Linux host needs the Xext and Xrandr development files (libxext-dev, libxrandr-dev).")
    endif()
    # Everything but the entry point, shared by the app and its debug build.
    add_library(usage_linux OBJECT
        src/linux/app.cpp
        src/linux/x11.cpp
    )
    target_link_libraries(usage_linux PUBLIC usage_renderer usage_host X11::X11 X11::Xext X11::Xrandr)
    add_executable(UsageTracker src/linux/main.cpp)
    target_link_libraries(UsageTracker PRIVATE usage_linux)
    # The same app with mock provider endpoints; middle-click cycles scenarios.
    add_executable(UsageTrackerDebug src/linux/main.cpp)
    target_compile_definitions(UsageTrackerDebug PRIVATE USAGETRACKER_MOCK)
    target_link_libraries(UsageTrackerDebug PRIVATE usage_linux)
    list(APPEND app_targets UsageTracker UsageTrackerDebug)
    if(BUILD_TESTING)
        # The live smoke test needs an X server; run it headless when Xvfb is available.
        find_program(XVFB_RUN xvfb-run)
        if(XVFB_RUN)
            add_test(NAME linux_smoke COMMAND "${XVFB_RUN}" -a "$<TARGET_FILE:UsageTracker>" --smoke-test)
            set_tests_properties(linux_smoke PROPERTIES TIMEOUT 60)
        endif()
    endif()
endif()

if(app_targets)
    install(TARGETS ${app_targets} RUNTIME DESTINATION .)
    install(FILES "${CLAY_WIDGETS_DIR}/subprojects/clay/LICENSE.md" DESTINATION licenses RENAME clay.txt)
    install(FILES "${CLAY_WIDGETS_DIR}/LICENSE" DESTINATION licenses RENAME clay-widgets.txt)
    install(FILES "${CLAY_WIDGETS_DIR}/subprojects/raylib/LICENSE" DESTINATION licenses RENAME raylib.txt)
    install(FILES "${CLAY_WIDGETS_DIR}/subprojects/raylib/src/external/glfw/LICENSE.md" DESTINATION licenses RENAME glfw.txt)
    install(FILES "${CLAY_WIDGETS_DIR}/subprojects/freetype/docs/FTL.TXT" DESTINATION licenses RENAME freetype.txt)
    install(FILES vendor/fonts/LICENSE.txt DESTINATION licenses RENAME roboto.txt)
    install(FILES vendor/README.md DESTINATION licenses RENAME dependencies.md)
    install(FILES vendor/nlohmann/LICENSE.MIT DESTINATION licenses RENAME nlohmann-json.txt)
endif()
