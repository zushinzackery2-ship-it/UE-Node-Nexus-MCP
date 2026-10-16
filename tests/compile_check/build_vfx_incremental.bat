@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if errorlevel 1 exit /b 1
set "NEXUS_VFX_HOST=%~dp0..\..\build\validation"
set "NEXUS_VFX_ENGINE=D:\Program Files\Epic Games\UE_5.5"
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0prepare_host.py" --host "%NEXUS_VFX_HOST%"
if errorlevel 1 exit /b 1
set "TEMP=%NEXUS_VFX_HOST%\Temp"
set "TMP=%TEMP%"
call "%NEXUS_VFX_ENGINE%\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="%NEXUS_VFX_HOST%\NexusValidation.uproject" -Module=UeNodeNexusVfxBridge -WaitMutex -NoHotReload -NoHotReloadFromIDE -NoLiveCoding -NoUBA -NoUBALocal -MaxParallelActions=2 -Log="%NEXUS_VFX_HOST%\Logs\VfxUserLinkBuild.log"
exit /b %errorlevel%
