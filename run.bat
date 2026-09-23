@echo off
setlocal
set "widget_exe=%~dp0bin\UsageTracker.exe"

if not exist "%widget_exe%" (
    echo Widget executable not found: "%widget_exe%"
    echo Run build.bat first.
    pause
    exit /b 1
)

taskkill /F /IM UsageTracker.exe >nul 2>&1
rem No existing process is normal; clear taskkill's error before checking start.
cmd /c exit 0
start "" /D "%~dp0bin" "%widget_exe%" %*
if errorlevel 1 (
    echo Failed to launch UsageTracker.
    pause
    exit /b 1
)
endlocal
