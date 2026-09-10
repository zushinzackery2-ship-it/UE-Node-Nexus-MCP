@echo off
setlocal
if not defined UE_NEXUS_ENGINE_DIR (
    echo ERROR: Set UE_NEXUS_ENGINE_DIR to your Unreal Engine 5.5 installation.
    exit /b 2
)
call "%~dp0vs_env.bat"
if errorlevel 1 exit /b 1
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0prepare_scene_tests.py"
if errorlevel 1 exit /b 1
set "NEXUS_SCENE_HOST=%~dp0..\..\build\scene-tests"
if not exist "%NEXUS_SCENE_HOST%\Temp" mkdir "%NEXUS_SCENE_HOST%\Temp"
if errorlevel 1 exit /b 1
set "TEMP=%NEXUS_SCENE_HOST%\Temp"
set "TMP=%TEMP%"
call "%UE_NEXUS_ENGINE_DIR%\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="%NEXUS_SCENE_HOST%\NexusValidation.uproject" -Module=UeNodeNexusBridge -Module=UeNodeNexusVfxBridge -WaitMutex -NoHotReload -NoHotReloadFromIDE -NoLiveCoding -NoUBA -NoUBALocal -MaxParallelActions=2 -Log="%NEXUS_SCENE_HOST%\Logs\UnrealBuildTool.log"
if errorlevel 1 exit /b 1
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0finalize_host.py" --host "%NEXUS_SCENE_HOST%" --engine-dir "%UE_NEXUS_ENGINE_DIR%"
exit /b %errorlevel%
