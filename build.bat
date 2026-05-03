@echo off
rem Build script for cmd text expander GUI

setlocal
set SRC=src\cmd_gui.cpp
set OUTDIR=publish
set OUTEXE=%OUTDIR%\cmd.exe

if not exist %OUTDIR% mkdir %OUTDIR%

where cl >nul 2>nul
if errorlevel 1 (
    echo MSVC cl.exe was not found. Run inside Developer Command Prompt or GitHub Actions windows-latest.
    exit /b 1
)

echo Compiling %SRC%...

rem Build the visible Win32 GUI version, not a black console window.
rem user32.lib is required for the GUI, keyboard hook, clipboard, and SendInput APIs.
rem gdi32.lib is linked for standard Windows GUI compatibility.
cl /nologo /EHsc /O2 /W3 %SRC% user32.lib gdi32.lib comctl32.lib /link /SUBSYSTEM:WINDOWS /OUT:%OUTEXE%

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
