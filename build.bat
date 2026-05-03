@echo off
setlocal
cd /d "%~dp0"
if exist publish rmdir /s /q publish
mkdir publish
where cl >nul 2>nul
if errorlevel 1 (
  echo MSVC cl.exe was not found. Run inside Developer Command Prompt or GitHub Actions windows-latest.
  exit /b 1
)
where rc >nul 2>nul
if errorlevel 1 (
  echo Windows resource compiler rc.exe was not found.
  exit /b 1
)
rc /nologo /fo publish\app.res src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE src\cmd_final.cpp publish\app.res /link /SUBSYSTEM:WINDOWS /OUT:publish\cmdTextExpander.exe user32.lib shell32.lib comdlg32.lib gdi32.lib comctl32.lib
if errorlevel 1 exit /b 1
if not exist publish\cmdTextExpander.exe (
  echo Build failed: publish\cmdTextExpander.exe was not created.
  exit /b 1
)
copy /y publish\cmdTextExpander.exe publish\cmd.exe >nul
echo Build complete: publish\cmdTextExpander.exe and publish\cmd.exe
