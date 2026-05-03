@echo off
setlocal
cd /d "%~dp0"
if exist publish rmdir /s /q publish
mkdir publish

where cl >nul 2>nul
if errorlevel 1 exit /b 1
where rc >nul 2>nul
if errorlevel 1 exit /b 1

rc /nologo /fo publish\app.res src\app.rc
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /O2 /EHsc /utf-8 /DNOMINMAX src\cmd.cpp publish\app.res /link /SUBSYSTEM:WINDOWS /OUT:publish\cmd.exe user32.lib shell32.lib comdlg32.lib gdi32.lib comctl32.lib
if errorlevel 1 exit /b 1

if not exist publish\cmd.exe exit /b 1
echo Build complete
