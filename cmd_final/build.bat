@echo off
rem Build script for cmd text expander
rem This batch file compiles src\cmd.cpp into a portable executable.

setlocal
set SRC=src\cmd.cpp
set OUTDIR=publish
set OUTEXE=%OUTDIR%\cmd.exe

if not exist %OUTDIR% mkdir %OUTDIR%

rem Locate the VC++ compiler (cl.exe).  Assume we are running from a
rem "Developer Command Prompt for Visual Studio" so cl.exe is in PATH.
echo Compiling %SRC%...
cl /nologo /EHsc /O2 /W3 %SRC% /link /SUBSYSTEM:WINDOWS /OUT:%OUTEXE%

if exist %OUTEXE% (
    echo Build succeeded: %OUTEXE%
) else (
    echo Build failed.
    exit /b 1
)
endlocal