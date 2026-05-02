@echo off
setlocal
cd /d "%~dp0"
dotnet restore src\CmdTextExpander\CmdTextExpander.csproj
if errorlevel 1 exit /b 1
dotnet publish src\CmdTextExpander\CmdTextExpander.csproj -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true /p:IncludeNativeLibrariesForSelfExtract=true /p:EnableCompressionInSingleFile=true /p:AssemblyName=cmd -o publish
if errorlevel 1 exit /b 1
if not exist publish\cmd.exe (
  echo Build failed: publish\cmd.exe was not created.
  exit /b 1
)
echo Build complete: publish\cmd.exe
