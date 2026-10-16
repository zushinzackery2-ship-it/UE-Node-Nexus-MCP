@echo off
setlocal
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
if errorlevel 1 exit /b 1
call "%~dp0build_baseline_incremental.bat"
exit /b %errorlevel%
