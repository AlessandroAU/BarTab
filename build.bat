@echo off
setlocal
set "configuration=%~1"
if not defined configuration set "configuration=Release"
if /I not "%configuration%"=="Release" if /I not "%configuration%"=="Debug" (
    echo Usage: build.bat [Release^|Debug] ["CMake generator"]
    exit /b 1
)
set "generator=%~2"
if not defined generator set "generator=Visual Studio 17 2022"
where cmake >nul 2>nul
if errorlevel 1 (
    echo Install CMake 3.22 or newer and add it to PATH.
    exit /b 1
)
cmake -S "%~dp0." -B "%~dp0build" -G "%generator%" -A x64
if errorlevel 1 (
    echo CMake configure failed. Install the Visual Studio Desktop development with C++ workload and Windows SDK.
    exit /b 1
)
cmake --build "%~dp0build" --config "%configuration%"
if errorlevel 1 exit /b 1
ctest --test-dir "%~dp0build" -C "%configuration%" --output-on-failure
if errorlevel 1 exit /b 1
cmake --install "%~dp0build" --config "%configuration%" --prefix "%~dp0bin"
if errorlevel 1 (
    echo Could not install the executable. Quit UsageTracker before rebuilding.
    exit /b 1
)
echo Built and tested %~dp0bin\UsageTracker.exe
exit /b 0
