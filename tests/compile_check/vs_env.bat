@echo off
if defined VSINSTALLDIR set "NEXUS_VS_ROOT=%VSINSTALLDIR%"
if defined NEXUS_VS_ROOT goto vs_ready
set "NEXUS_VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%NEXUS_VSWHERE_DIR%\vswhere.exe" exit /b 2
pushd "%NEXUS_VSWHERE_DIR%"
if errorlevel 1 exit /b 2
for /f "tokens=*" %%I in ('vswhere.exe -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "NEXUS_VS_ROOT=%%I"
popd
:vs_ready
if not exist "%NEXUS_VS_ROOT%\Common7\Tools\VsDevCmd.bat" (
    echo ERROR: Visual Studio 2022 with C++ build tools is required.
    exit /b 2
)
call "%NEXUS_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=amd64
exit /b %errorlevel%
