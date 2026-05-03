@echo off
setlocal
cd /d "%~dp0"

if exist publish rmdir /s /q publish
mkdir publish

xcopy app\* publish\ /E /I /Y >nul

if exist publish\cmd.exe (
    echo Package complete: publish\cmd.exe
) else (
    echo Package failed: publish\cmd.exe was not created.
    exit /b 1
)

endlocal
