@echo off
rem Removes the RedXe shortcut, sign-in entry, Apps entry, and the installed copy. Settings stay unless: uninstall.cmd -PurgeUserData
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-RedXe.ps1" -Action Remove %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Removal failed with exit code %result%.
rem Keep the window open when Explorer started this file by double-click.
echo %CMDCMDLINE% | find /i " /c " >nul 2>&1 && pause
exit /b %result%
