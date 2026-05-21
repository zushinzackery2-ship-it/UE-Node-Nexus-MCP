@echo off
setlocal

set "UE_ENGINE=D:\Program Files\Epic Games\UE_5.5\Engine"
set "PROJECT_ROOT=%~dp0.."
set "PLUGIN_PATH=%PROJECT_ROOT%\Plugins\UeNodeNexusBridge\UeNodeNexusBridge.uplugin"
set "PACKAGE_DIR=%PROJECT_ROOT%\bin\UeNodeNexusBridge"
set "HOST_PROJECT_DIR=%PROJECT_ROOT%\bin\UeNodeNexusBridgeHost"
set "HOST_PROJECT=%HOST_PROJECT_DIR%\UeNodeNexusBridgeHost.uproject"
set "HOST_PLUGIN_DIR=%HOST_PROJECT_DIR%\Plugins\UeNodeNexusBridge"
set "HOST_SOURCE_DIR=%HOST_PROJECT_DIR%\Source\UeNodeNexusBridgeHost"
set "UBT_DLL=%UE_ENGINE%\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
set "APPDATA=%PROJECT_ROOT%\obj\AppData\Roaming"
set "LOCALAPPDATA=%PROJECT_ROOT%\obj\AppData\Local"
set "PROGRAMDATA=%PROJECT_ROOT%\obj\ProgramData"
set "uebp_LogFolder=%PROJECT_ROOT%\obj\UATLogs"
set "UBT_CONFIG_DIR=%HOST_PROJECT_DIR%\Saved\UnrealBuildTool"

if not exist "%APPDATA%" mkdir "%APPDATA%"
if not exist "%LOCALAPPDATA%" mkdir "%LOCALAPPDATA%"
if not exist "%PROGRAMDATA%" mkdir "%PROGRAMDATA%"
if not exist "%uebp_LogFolder%" mkdir "%uebp_LogFolder%"
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

set "APPDATA=%PROJECT_ROOT%\obj\AppData\Roaming"
set "LOCALAPPDATA=%PROJECT_ROOT%\obj\AppData\Local"
set "PROGRAMDATA=%PROJECT_ROOT%\obj\ProgramData"

if not exist "%UBT_DLL%" (
    echo UE5.5 UnrealBuildTool not found: "%UBT_DLL%"
    exit /b 1
)

if exist "%PACKAGE_DIR%" (
    rmdir /s /q "%PACKAGE_DIR%"
)

if exist "%HOST_PROJECT_DIR%" (
    rmdir /s /q "%HOST_PROJECT_DIR%"
)

mkdir "%HOST_PROJECT_DIR%"
mkdir "%HOST_PROJECT_DIR%\Plugins"
mkdir "%HOST_SOURCE_DIR%"
xcopy "%PROJECT_ROOT%\Plugins\UeNodeNexusBridge" "%HOST_PLUGIN_DIR%\" /E /I /Y
if errorlevel 1 exit /b 1

(
    echo {
    echo   "FileVersion": 3,
    echo   "DisableEnginePluginsByDefault": true,
    echo   "Plugins": [
    echo     { "Name": "UeNodeNexusBridge", "Enabled": true }
    echo   ]
    echo }
) > "%HOST_PROJECT%"

(
    echo using UnrealBuildTool;
    echo.
    echo public class UeNodeNexusBridgeHostEditorTarget : TargetRules
    echo {
    echo     public UeNodeNexusBridgeHostEditorTarget^(TargetInfo Target^) : base^(Target^)
    echo     {
    echo         Type = TargetType.Editor;
    echo         DefaultBuildSettings = BuildSettingsVersion.V5;
    echo         IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
    echo         bAllowEnginePluginsEnabledByDefault = false;
    echo         DisablePlugins.Add^("CaptureData"^);
    echo         EnablePlugins.Add^("UeNodeNexusBridge"^);
    echo         ExtraModuleNames.Add^("UeNodeNexusBridgeHost"^);
    echo     }
    echo }
) > "%HOST_PROJECT_DIR%\Source\UeNodeNexusBridgeHostEditor.Target.cs"

(
    echo using UnrealBuildTool;
    echo.
    echo public class UeNodeNexusBridgeHost : ModuleRules
    echo {
    echo     public UeNodeNexusBridgeHost^(ReadOnlyTargetRules Target^) : base^(Target^)
    echo     {
    echo         PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
    echo         PrivateDependencyModuleNames.AddRange^(new string[] { "Core", "CoreUObject", "Engine" }^);
    echo     }
    echo }
) > "%HOST_SOURCE_DIR%\UeNodeNexusBridgeHost.Build.cs"

(
    echo #include "Modules/ModuleManager.h"
    echo IMPLEMENT_PRIMARY_GAME_MODULE^(FDefaultGameModuleImpl, UeNodeNexusBridgeHost, "UeNodeNexusBridgeHost"^);
) > "%HOST_SOURCE_DIR%\UeNodeNexusBridgeHost.cpp"

if not exist "%UBT_CONFIG_DIR%" mkdir "%UBT_CONFIG_DIR%"

(
    echo ^<?xml version="1.0" encoding="utf-8"?^>
    echo ^<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"^>
    echo   ^<BuildConfiguration^>
    echo     ^<bAllowUBAExecutor^>false^</bAllowUBAExecutor^>
    echo     ^<bAllowUBALocalExecutor^>false^</bAllowUBALocalExecutor^>
    echo     ^<bAllowHotReloadFromIDE^>false^</bAllowHotReloadFromIDE^>
    echo     ^<bUsePrecompiled^>true^</bUsePrecompiled^>
    echo     ^<bSkipRulesCompile^>true^</bSkipRulesCompile^>
  echo   ^</BuildConfiguration^>
    echo ^</Configuration^>
) > "%UBT_CONFIG_DIR%\BuildConfiguration.xml"

set "DOTNET_EXE=%UE_ENGINE%\Binaries\ThirdParty\DotNet\8.0.300\win-x64\dotnet.exe"
call "%DOTNET_EXE%" "%UBT_DLL%" UeNodeNexusBridgeHostEditor Win64 Development -Project="%HOST_PROJECT%" -NoUBA -NoUBALocal -NoHotReload -NoHotReloadFromIDE -log="%uebp_LogFolder%\UBT-UeNodeNexusBridgeHost.txt"
if errorlevel 1 exit /b %ERRORLEVEL%

mkdir "%PACKAGE_DIR%"
xcopy "%HOST_PLUGIN_DIR%" "%PACKAGE_DIR%\" /E /I /Y /EXCLUDE:%PROJECT_ROOT%\scripts\plugin_package_exclude.txt
exit /b %ERRORLEVEL%
