@echo off
setlocal
cd /d "%~dp0"
echo.
echo Veysona Studio - Genuine recorded piano, guitar and acoustic drum packs
where py >nul 2>&1
if errorlevel 1 (
  echo Python 3 launcher not found. Install Python 3 before continuing.
  pause
  exit /b 1
)
where ffmpeg >nul 2>&1
if errorlevel 1 (
  echo FFmpeg is not available on PATH.
  echo Install using: winget install Gyan.FFmpeg
  echo Then reopen this window and try again.
  pause
  exit /b 1
)
py -3 tools\install_recorded_instruments.py --quality compact
if errorlevel 1 (
  echo Installation failed. Please read RECORDED_INSTRUMENTS.md for details.
) else (
  echo Recorded sounds are now installed. Restart Veysona Studio.
)
pause
