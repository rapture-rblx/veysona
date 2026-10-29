@echo off
setlocal
cd /d "%~dp0"
echo ============================================
echo      Veysona Studio v1.31 - Windows Build
echo ============================================
where cmake >nul 2>nul
if errorlevel 1 (echo ERROR: Install CMake and Visual Studio 2022 C++ tools.& exit /b 1)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DVEYSONA_BUILD_GUI=ON -DVEYSONA_BUILD_BUNDLED_VST3=ON
if errorlevel 1 (echo CONFIGURE FAILED. Check network access to GitHub for JUCE 8.0.10.& exit /b 1)
cmake --build build --config Release --parallel 4
if errorlevel 1 (echo BUILD FAILED. Check compiler output above.& exit /b 1)
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 (echo TEST FAILED.& exit /b 1)
echo.
echo Copying original VST3 bundles beside the application...
if not exist "build\Veysona_artefacts\Release\VST3" mkdir "build\Veysona_artefacts\Release\VST3"
for %%P in (VeysonaPrism VeysonaEcho VeysonaEQ VeysonaCompressor VeysonaReverb) do (
 if exist "build\%%P_artefacts\Release\VST3" xcopy "build\%%P_artefacts\Release\VST3\*" "build\Veysona_artefacts\Release\VST3\" /E /I /Y >nul
)
echo Offline audio production CLI: build\Release\veysona_audio_tool.exe
echo BUILD SUCCESS. Find Veysona.exe in build\Veysona_artefacts\Release\
echo Five VST3 plugin builds are in their plugin artefact folders.
echo Audio samples are copied alongside Veysona.exe.
endlocal
