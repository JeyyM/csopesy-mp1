@echo off
setlocal

set "ROOT=%~dp0"
set "OUT_DIR=%ROOT%"
set "OUT_EXE=%OUT_DIR%\csopesy_os_mp.exe"

if not exist "%OUT_DIR%" (
    mkdir "%OUT_DIR%"
)

where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ was not found on PATH.
    echo Install MSYS2/MinGW-w64 or add g++ to PATH, then run this script again.
    exit /b 1
)

g++ -std=c++17 -I "%ROOT%src" ^
    "%ROOT%src\main.cpp" ^
    "%ROOT%src\Config.cpp" ^
    "%ROOT%src\ConsoleManager.cpp" ^
    "%ROOT%src\ProcessModel.cpp" ^
    "%ROOT%src\ReportManager.cpp" ^
    "%ROOT%src\OutputManager.cpp" ^
    "%ROOT%src\ScreenManager.cpp" ^
    "%ROOT%src\Scheduler.cpp" ^
    "%ROOT%src\InstructionEngine.cpp" ^
    "%ROOT%src\MemoryManager.cpp" ^
    "%ROOT%src\TimeUtil.cpp" ^
    -o "%OUT_EXE%"

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo Built "%OUT_EXE%"
endlocal
