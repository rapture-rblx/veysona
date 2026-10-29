@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish_to_github.ps1"
if errorlevel 1 (
  echo.
  echo Publish failed. Read the error above, install Git for Windows and GitHub CLI if missing, and try again.
)
echo.
pause
