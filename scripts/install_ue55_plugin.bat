@echo off
setlocal

set "UE_ENGINE=D:\Program Files\Epic Games\UE_5.5\Engine"
set "PROJECT_ROOT=%~dp0.."
set "PLUGIN_SOURCE=%PROJECT_ROOT%\bin\UeNodeNexusBridge"
set "PLUGIN_TARGET=%UE_ENGINE%\Plugins\Marketplace\UeNodeNexusBridge"

if not exist "%UE_ENGINE%\Plugins" (
    echo UE5.5 Engine Plugins directory not found: "%UE_ENGINE%\Plugins"
    exit /b 1
)

if not exist "%PLUGIN_SOURCE%\UeNodeNexusBridge.uplugin" (
    echo Packaged plugin not found: "%PLUGIN_SOURCE%"
    echo Run scripts\build_plugin_ue55.bat before installing.
    exit /b 1
)

if not exist "%UE_ENGINE%\Plugins\Marketplace" (
    mkdir "%UE_ENGINE%\Plugins\Marketplace"
    if errorlevel 1 exit /b 1
)

if exist "%PLUGIN_TARGET%" (
    rmdir /s /q "%PLUGIN_TARGET%"
    if errorlevel 1 exit /b 1
)

xcopy "%PLUGIN_SOURCE%" "%PLUGIN_TARGET%\" /E /I /Y
exit /b %ERRORLEVEL%
