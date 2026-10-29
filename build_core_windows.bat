@echo off
setlocal
cd /d "%~dp0"
echo =======================================================
echo   Veysona Studio 1.5 - Offline C++ Tools and Tests
 echo   JUCE and VST3 dependencies are NOT required
 echo =======================================================
where cmake >nul 2>nul
if errorlevel 1 (echo ERROR: CMake 3.22+ is required. & exit /b 1)
cmake -S . -B build-core-win -G "Visual Studio 17 2022" -A x64 -DVEYSONA_BUILD_GUI=OFF -DBUILD_TESTING=ON
if errorlevel 1 (echo CONFIGURE FAILED.& exit /b 1)
cmake --build build-core-win --config Release --parallel 4
if errorlevel 1 (echo BUILD FAILED.& exit /b 1)
ctest --test-dir build-core-win -C Release --output-on-failure
if errorlevel 1 (echo TEST FAILED.& exit /b 1)
echo.
echo Windows audio tools compiled and tests passed.
echo See build-core-win\Release\veysona_audio_tool.exe
endlocal
