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
call "%~dp0vs_env.bat"
if errorlevel 1 exit /b 1

set "VALIDATION_ROOT=%~dp0..\..\build\validation"
if not exist "%VALIDATION_ROOT%\Logs" mkdir "%VALIDATION_ROOT%\Logs"
if errorlevel 1 exit /b 1
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0prepare_host.py" --host "%VALIDATION_ROOT%"
if errorlevel 1 exit /b 1

if not exist "%VALIDATION_ROOT%\Temp" mkdir "%VALIDATION_ROOT%\Temp"
if errorlevel 1 exit /b 1
set "TEMP=%VALIDATION_ROOT%\Temp"
set "TMP=%TEMP%"
call "%UE_NEXUS_ENGINE_DIR%\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="%VALIDATION_ROOT%\NexusValidation.uproject" -Module=UeNodeNexusBridge -Module=UeNodeNexusVfxBridge -WaitMutex -NoHotReload -NoHotReloadFromIDE -NoLiveCoding -NoUBA -NoUBALocal -MaxParallelActions=2 -Log="%VALIDATION_ROOT%\Logs\UnrealBuildTool.log"
if errorlevel 1 exit /b 1
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0finalize_host.py" --host "%VALIDATION_ROOT%" --engine-dir "%UE_NEXUS_ENGINE_DIR%"
exit /b %errorlevel%
