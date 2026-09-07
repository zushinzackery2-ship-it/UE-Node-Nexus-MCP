@echo off
setlocal
if not defined UE_NEXUS_ENGINE_DIR (
    echo ERROR: Set UE_NEXUS_ENGINE_DIR to your Unreal Engine 5.5 installation.
    exit /b 2
)
if not exist "%UE_NEXUS_ENGINE_DIR%\Engine\Build\BatchFiles\Build.bat" (
    echo ERROR: UE_NEXUS_ENGINE_DIR does not contain Engine\Build\BatchFiles\Build.bat.
    exit /b 2
)
if defined VSINSTALLDIR set "NEXUS_VS_ROOT=%VSINSTALLDIR%"
if defined NEXUS_VS_ROOT goto vs_ready
set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" (
    echo ERROR: Visual Studio Installer and vswhere are required.
    exit /b 2
)
pushd "%VSWHERE_DIR%"
if errorlevel 1 exit /b 2
for /f "tokens=*" %%I in ('vswhere.exe -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "NEXUS_VS_ROOT=%%I"
popd
:vs_ready
if not exist "%NEXUS_VS_ROOT%\Common7\Tools\VsDevCmd.bat" (
    echo ERROR: Visual Studio 2022 with C++ build tools is required.
    exit /b 2
)
call "%NEXUS_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=amd64
if errorlevel 1 exit /b 1

set "VALIDATION_ROOT=%~dp0..\..\build\validation"
if not exist "%VALIDATION_ROOT%\Logs" mkdir "%VALIDATION_ROOT%\Logs"
if errorlevel 1 exit /b 1
copy /Y "%~dp0fixtures\NexusValidation.uproject" "%VALIDATION_ROOT%\NexusValidation.uproject" >nul
if errorlevel 1 exit /b 1

for %%P in (UeNodeNexusBridge UeNodeNexusVfxBridge) do (
    robocopy "%~dp0..\..\Plugins\%%P" "%VALIDATION_ROOT%\Plugins\%%P" /E /XD Binaries Intermediate /NFL /NDL /NJH /NJS /NP
    if errorlevel 8 exit /b 1
)

call "%UE_NEXUS_ENGINE_DIR%\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="%VALIDATION_ROOT%\NexusValidation.uproject" -WaitMutex -NoHotReload -MaxParallelActions=4 -Log="%VALIDATION_ROOT%\Logs\UnrealBuildTool.log"
exit /b %errorlevel%
