@echo off
rem Build script for chrome Text Expander v21 GUI

setlocal
cd /d "%~dp0"

set SRC=src\chrome_gui.cpp
set OUTDIR=publish
set OUTEXE=%OUTDIR%\chrome.exe

if exist %OUTDIR% rmdir /s /q %OUTDIR%
mkdir %OUTDIR%

where cl >nul 2>nul
if errorlevel 1 (
    echo MSVC cl.exe was not found. Run inside Developer Command Prompt or GitHub Actions windows-latest.
    exit /b 1
)

echo Compiling %SRC%...

cl /nologo /EHsc /O2 /W3 /DUNICODE /D_UNICODE %SRC% user32.lib gdi32.lib comctl32.lib /link /SUBSYSTEM:WINDOWS /OUT:%OUTEXE%

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if exist %OUTEXE% (
    echo Build succeeded: %OUTEXE%
) else (
    echo Build failed: %OUTEXE% was not created.
    exit /b 1
)

endlocal
