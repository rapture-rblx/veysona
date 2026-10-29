@echo off
setlocal
cd /d "%~dp0"
echo ==================================================
echo     Veysona Studio v1.31 - Windows Setup Builder
echo ==================================================
where cmake >nul 2>nul
if errorlevel 1 (echo ERROR: CMake not found in PATH.& exit /b 1)
where powershell >nul 2>nul
if errorlevel 1 (echo ERROR: Windows PowerShell not found.& exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File ".\scripts\build_installer.ps1"
if errorlevel 1 (echo ERROR: Build or validation failed. Do not distribute an older setup file.& exit /b 1)
echo.
echo See dist\Veysona-Studio-Setup-1.31.0.exe
endlocal
