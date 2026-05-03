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
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE src\cmd.cpp /link /SUBSYSTEM:WINDOWS /OUT:publish\cmd.exe user32.lib shell32.lib comdlg32.lib gdi32.lib
if errorlevel 1 exit /b 1
if not exist publish\cmd.exe (
  echo Build failed: publish\cmd.exe was not created.
  exit /b 1
)
echo Build complete: publish\cmd.exe
