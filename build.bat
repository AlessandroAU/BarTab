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
rem A C++ compiler is required; CMake fails late and cryptically without one.
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "compiler="
if exist "%vswhere%" (
    for /f "usebackq delims=" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Workload.VCTools -requiresAny -property installationPath`) do set "compiler=%%i"
)
if not defined compiler (
    where cl >nul 2>nul && set "compiler=cl on PATH"
)
if not defined compiler (
    echo No MSVC C++ compiler found. BarTab needs the Visual Studio C++ build tools.
    where winget >nul 2>nul
    if errorlevel 1 (
        echo Install them from https://visualstudio.microsoft.com/downloads/ ^(Desktop development with C++^).
        exit /b 1
    )
    echo.
    echo The following command installs them ^(a few GB, several minutes^):
    echo   winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    echo.
    rem choice, not set /p: %variables% in this block expand before the prompt runs.
    choice /C YN /N /M "Run it now? [Y/N] "
    if errorlevel 2 (
        echo Aborted. Install the C++ build tools, then run build.bat again.
        exit /b 1
    )
    winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    if errorlevel 1 (
        echo The install did not finish. Install the C++ build tools, then run build.bat again.
        exit /b 1
    )
)
cmake -S "%~dp0." -B "%~dp0build" -G "%generator%" -A x64
if errorlevel 1 (
    echo CMake configure failed. Install the Visual Studio Desktop development with C++ workload and Windows SDK.
    exit /b 1
)
rem Stop installed and build-tree instances so their executables are not locked.
taskkill /F /IM BarTab.exe >nul 2>&1
taskkill /F /IM BarTabDebug.exe >nul 2>&1
taskkill /F /IM BarTabDebug.exe >nul 2>&1
rem No existing process is normal; the build command supplies the next exit code.
cmake --build "%~dp0build" --config "%configuration%"
if errorlevel 1 exit /b 1
ctest --test-dir "%~dp0build" -C "%configuration%" --output-on-failure
if errorlevel 1 exit /b 1
cmake --install "%~dp0build" --config "%configuration%" --prefix "%~dp0bin"
if errorlevel 1 (
    echo Could not install the executable. Quit BarTab before rebuilding.
    exit /b 1
)
echo Built and tested %~dp0bin\BarTab.exe and the mock-provider BarTabDebug.exe
exit /b 0
