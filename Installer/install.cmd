@echo off
rem Installs RedXe from this folder: copy to %LocalAppData%\Programs\RedXe (or register a winget package in place),
rem Start Menu shortcut, Settings ^> Apps entry. Extra switches pass through, for example: install.cmd -StartAtSignIn -Launch
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-RedXe.ps1" -Action Install %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Installation failed with exit code %result%.
rem Keep the window open when Explorer started this file by double-click.
echo %CMDCMDLINE% | find /i " /c " >nul 2>&1 && pause
exit /b %result%
