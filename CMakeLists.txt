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
    add_compile_options(/W4 /WX /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_library(usage_core STATIC src/core/usage.cpp src/core/codex.cpp src/core/claude.cpp)
target_include_directories(usage_core SYSTEM PRIVATE vendor/nlohmann)
if(MSVC)
    target_compile_definitions(usage_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_include_directories(usage_core PUBLIC src)

# Vendored sources: offline builds, no Git checkout or network fetch required.
add_library(clay_widgets STATIC src/ui/clay_widgets.cpp)
target_include_directories(clay_widgets SYSTEM PUBLIC vendor/clay vendor/clay-widgets)
target_compile_features(clay_widgets PUBLIC cxx_std_20)
if(MSVC)
    # Suppress upstream warnings only in the third-party implementation TU.
    target_compile_options(clay_widgets PRIVATE /W0 /WX-)
    target_compile_definitions(clay_widgets PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(clay_widgets PRIVATE -w)
endif()

add_library(usage_ui STATIC src/ui/views.cpp)
target_include_directories(usage_ui PUBLIC src)
target_link_libraries(usage_ui PUBLIC usage_core clay_widgets)

include(CTest)
if(BUILD_TESTING)
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
    add_executable(clay_widgets_tests vendor/clay-widgets/tests/test-widgets.cpp)
    target_include_directories(clay_widgets_tests PRIVATE vendor/clay vendor/clay-widgets)
    set_target_properties(clay_widgets_tests PROPERTIES CXX_STANDARD 20)
    if(MSVC)
        target_compile_options(clay_widgets_tests PRIVATE /W0 /WX-)
        target_compile_definitions(clay_widgets_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(clay_widgets_tests PRIVATE -w)
    endif()
    add_test(NAME clay_widgets COMMAND clay_widgets_tests)
    add_executable(usage_core_tests tests/core_tests.cpp)
    target_link_libraries(usage_core_tests PRIVATE usage_core)
    add_test(NAME usage_core COMMAND usage_core_tests)
endif()

if(WIN32)
    enable_language(RC)
    if(NOT MSVC)
        message(FATAL_ERROR "The Windows host currently requires MSVC and the Windows SDK. The portable core supports GCC/Clang on Linux.")
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
        add_subdirectory(vendor/raylib EXCLUDE_FROM_ALL)
    endfunction()
    get_directory_property(app_compile_options COMPILE_OPTIONS)
    add_raylib()
    set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${app_compile_options}")
    add_library(usage_renderer STATIC src/ui/raylib_renderer.cpp)
    # raylib's C API name collides with user32's CloseWindow in a native host.
    target_compile_definitions(raylib PRIVATE CloseWindow=UsageTrackerRaylibCloseWindow)
    target_compile_definitions(usage_renderer PRIVATE CloseWindow=UsageTrackerRaylibCloseWindow)
    target_include_directories(usage_renderer SYSTEM PRIVATE vendor/clay-widgets vendor/fonts)
    target_link_libraries(usage_renderer PUBLIC usage_ui PRIVATE raylib)
    add_executable(UsageTracker WIN32
        src/windows/resources.rc
        src/windows/main.cpp
        src/windows/app.cpp
        src/windows/platform.cpp
        src/windows/settings.cpp
        src/windows/providers.cpp
        src/windows/discovery.cpp
        src/windows/startup.cpp
        src/windows/widget.cpp
        src/windows/popup.cpp
        src/windows/smoke_test.cpp
        src/windows/taskbar.cpp
    )
    target_compile_definitions(UsageTracker PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_include_directories(UsageTracker SYSTEM PRIVATE vendor/nlohmann)
    target_include_directories(UsageTracker PRIVATE assets)
    target_link_libraries(UsageTracker PRIVATE usage_renderer user32 gdi32 shell32 ole32 oleaut32 uiautomationcore dwmapi advapi32)
    target_link_options(UsageTracker PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")

    # Regenerates the README images offscreen. Not part of the test suite: it
    # needs a GPU context and writes into the source tree.
    add_executable(screenshots src/tools/screenshots.cpp)
    target_link_libraries(screenshots PRIVATE usage_renderer)
    add_custom_target(docs-screenshots
        COMMAND screenshots "${CMAKE_CURRENT_SOURCE_DIR}/docs/images"
        DEPENDS screenshots
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Rendering README screenshots")
    if(BUILD_TESTING)
        add_executable(discovery_tests tests/discovery_tests.cpp src/windows/discovery.cpp)
        target_include_directories(discovery_tests PRIVATE src)
        add_test(NAME executable_discovery COMMAND discovery_tests)
        add_executable(startup_tests tests/startup_tests.cpp src/windows/startup.cpp)
        target_include_directories(startup_tests PRIVATE src)
        target_link_libraries(startup_tests PRIVATE advapi32)
        add_test(NAME startup_toggle COMMAND startup_tests)
    endif()
    install(TARGETS UsageTracker RUNTIME DESTINATION .)
    install(FILES vendor/clay/LICENSE.md DESTINATION licenses RENAME clay.txt)
    install(FILES vendor/clay-widgets/LICENSE DESTINATION licenses RENAME clay-widgets.txt)
    install(FILES vendor/raylib/LICENSE DESTINATION licenses RENAME raylib.txt)
    install(FILES vendor/raylib/src/external/glfw/LICENSE.md DESTINATION licenses RENAME glfw.txt)
    install(FILES vendor/fonts/LICENSE.txt DESTINATION licenses RENAME roboto.txt)
    install(FILES vendor/README.md DESTINATION licenses RENAME dependencies.md)
    install(FILES vendor/nlohmann/LICENSE.MIT DESTINATION licenses RENAME nlohmann-json.txt)
else()
    message(STATUS "Building portable core and tests. A Linux desktop host is not implemented yet.")
endif()
