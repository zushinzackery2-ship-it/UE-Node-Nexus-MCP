@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
if errorlevel 1 exit /b 1
set "NEXUS_CONTRACTS_HOST=%~dp0..\..\build\validation"
set "NEXUS_CONTRACTS_ENGINE=D:\Program Files\Epic Games\UE_5.5"
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0prepare_host.py" --host "%NEXUS_CONTRACTS_HOST%"
if errorlevel 1 exit /b 1
set "TEMP=%NEXUS_CONTRACTS_HOST%\Temp"
set "TMP=%TEMP%"
call "%NEXUS_CONTRACTS_ENGINE%\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="%NEXUS_CONTRACTS_HOST%\NexusValidation.uproject" -Module=UeNodeNexusGuard -Module=UeNodeNexusBridge -WaitMutex -NoHotReload -NoHotReloadFromIDE -NoLiveCoding -NoUBA -NoUBALocal -MaxParallelActions=2 -Log="%NEXUS_CONTRACTS_HOST%\Logs\BlueprintContractsBuild.log"
exit /b %errorlevel%
