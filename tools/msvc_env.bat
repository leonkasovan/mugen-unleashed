@echo off
rem Sets up the MSVC x86 (32-bit) build environment, then runs the given command.
rem Usage: tools\msvc_env.bat cmake --preset x86-release
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
if not defined VSINSTALLDIR (
  for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
)
if not defined VSINSTALLDIR call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
if "%~1"=="" exit /b 0
%*
