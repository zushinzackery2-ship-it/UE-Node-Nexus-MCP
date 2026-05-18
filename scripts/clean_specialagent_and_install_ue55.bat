@echo off
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0clean_specialagent.ps1"
if errorlevel 1 exit /b 1

call "%~dp0install_ue55_plugin.bat"
exit /b %ERRORLEVEL%

