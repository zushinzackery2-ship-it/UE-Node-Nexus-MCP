@echo off
setlocal
if not defined UE_NEXUS_ENGINE_DIR exit /b 2
call "%~dp0vs_env.bat"
if errorlevel 1 exit /b 1
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0prepare_installed_project.py"
if errorlevel 1 exit /b 1
call "%UE_NEXUS_ENGINE_DIR%\Engine\Build\BatchFiles\Build.bat" NexusCppProbeEditor Win64 Development -Project="%~dp0..\..\build\installed-project\NexusCppProbe.uproject" -WaitMutex -NoHotReload -NoHotReloadFromIDE -NoLiveCoding -NoUBA -NoUBALocal -MaxParallelActions=2 -Log="%~dp0..\..\build\installed-project\UnrealBuildTool.log"
exit /b %errorlevel%
