@echo off
rem Build script for cmd text expander
rem This batch file compiles src\cmd.cpp into a portable executable.

setlocal
set SRC=src\cmd.cpp
set OUTDIR=publish
set OUTEXE=%OUTDIR%\cmd.exe

if not exist %OUTDIR% mkdir %OUTDIR%

where cl >nul 2>nul
if errorlevel 1 (
    echo MSVC cl.exe was not found. Run inside Developer Command Prompt or GitHub Actions windows-latest.
    exit /b 1
)

rem Locate the VC++ compiler (cl.exe). Assume we are running from a
rem Developer Command Prompt for Visual Studio or GitHub Actions windows-latest.
echo Compiling %SRC%...

rem user32.lib is required for keyboard hook, input, message loop, hotkey,
rem clipboard, and layout APIs such as ToUnicodeEx, GetMessageW,
rem RegisterHotKey, OpenClipboard, SendInput, SetWindowsHookExW, etc.
rem gdi32.lib is linked for standard Windows GUI compatibility.
rem
rem IMPORTANT:
rem Build as a CONSOLE subsystem with wWinMainCRTStartup so the program has
rem a visible window while it runs. The previous WINDOWS subsystem build was
rem working in the background but did not show any visible interface.
cl /nologo /EHsc /O2 /W3 %SRC% user32.lib gdi32.lib /link /SUBSYSTEM:CONSOLE /ENTRY:wWinMainCRTStartup /OUT:%OUTEXE%

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
