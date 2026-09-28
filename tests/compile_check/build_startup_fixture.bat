@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
if errorlevel 1 exit /b 1
if not defined UE_NEXUS_ENGINE_DIR set "UE_NEXUS_ENGINE_DIR=D:\Program Files\Epic Games\UE_5.5"
set "FIXTURE_HOST=%~dp0..\..\build\validation"
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0prepare_startup_fixture.py"
if errorlevel 1 exit /b 1
call "%UE_NEXUS_ENGINE_DIR%\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="%FIXTURE_HOST%\NexusValidation.uproject" -Module=UeNexusStartupFixture -WaitMutex -NoHotReload -NoLiveCoding -NoUBA -NoUBALocal -MaxParallelActions=2 -Log="%FIXTURE_HOST%\Logs\StartupFixtureBuild.log"
if errorlevel 1 exit /b 1
"%~dp0..\..\.venv\Scripts\python.exe" "%~dp0prepare_startup_fixture.py" --finalize
exit /b %errorlevel%
