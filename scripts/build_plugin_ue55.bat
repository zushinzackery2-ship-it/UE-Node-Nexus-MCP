@echo off
setlocal

set "UE_ENGINE=D:\Program Files\Epic Games\UE_5.5\Engine"
set "PROJECT_ROOT=%~dp0.."
set "PLUGIN_PATH=%PROJECT_ROOT%\Plugins\UeNodeNexusBridge\UeNodeNexusBridge.uplugin"
set "PACKAGE_DIR=%PROJECT_ROOT%\bin\UeNodeNexusBridge"
set "APPDATA=%PROJECT_ROOT%\obj\AppData\Roaming"
set "LOCALAPPDATA=%PROJECT_ROOT%\obj\AppData\Local"
set "uebp_LogFolder=%PROJECT_ROOT%\obj\UATLogs"

if not exist "%APPDATA%" mkdir "%APPDATA%"
if not exist "%LOCALAPPDATA%" mkdir "%LOCALAPPDATA%"
if not exist "%uebp_LogFolder%" mkdir "%uebp_LogFolder%"

call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

if not exist "%UE_ENGINE%\Build\BatchFiles\RunUAT.bat" (
    echo UE5.5 RunUAT not found: "%UE_ENGINE%\Build\BatchFiles\RunUAT.bat"
    exit /b 1
)

if exist "%PACKAGE_DIR%" (
    rmdir /s /q "%PACKAGE_DIR%"
)

call "%UE_ENGINE%\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="%PLUGIN_PATH%" -Package="%PACKAGE_DIR%" -Rocket
exit /b %ERRORLEVEL%
