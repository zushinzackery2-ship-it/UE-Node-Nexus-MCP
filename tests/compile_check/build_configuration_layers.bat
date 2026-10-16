@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
if errorlevel 1 exit /b 1
set "UE_NEXUS_ENGINE_DIR=D:\Program Files\Epic Games\UE_5.5"
set "NEXUS_VS_ROOT=D:\Program Files\Microsoft Visual Studio\2022\Community"
call "%~dp0build_plugins.bat"
exit /b %errorlevel%
