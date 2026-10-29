# Veysona Studio v1.31 — Full source build

One complete source ZIP includes the Veysona application source, JUCE CMake configuration, Windows installer files, effects and instruments, **all 2,033 sample WAVs and 456 sampled-instrument WAVs stored losslessly as FLAC**, 126 MIDI loops, 212 effect presets and all tests. **CMake automatically recreates the original WAV files with SHA-256 verification before compiling.** The separately downloaded **real recorded** acoustic piano/guitars/drum kits are still optional CC0 sound packs and are not pre-bundled; use `Install_Recorded_Instruments.cmd` after building.

## Build the Windows GUI

Requires Windows 10/11 x64, Visual Studio 2022 (Desktop development with C++), Windows SDK, CMake 3.22+, Git. JUCE 8.0.10 is fetched at configure time.

Open a VS 2022 x64 Native Tools Command Prompt in the source folder:

```bat
build_windows.bat
```

Alternative direct build:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DVEYSONA_BUILD_GUI=ON -DVEYSONA_BUILD_BUNDLED_VST3=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Output: `build\Veysona_artefacts\Release\Veysona.exe`. To create Inno Setup packages, install Inno Setup 6 and run `build_release_windows.cmd`.

## Publish updates to users

The splash says **Checking for updates...**. The automatic installer needs **two build settings**: the actual public HTTPS update manifest and the publisher's Windows signing-certificate thumbprint. Without them, the source still builds, but no release is automatically installed. See `PUBLISH_UPDATES_v1.31.md` for code signing, a template for `updates/stable.json`, the GitHub Releases workflow, and certificate pinning.

## Notes

- Optional ASIO SDK is **not included**. WASAPI remains the default.
- Do not run test-only CLI outputs expecting a GUI.
- The 47/47 passing portable suite does not validate JUCE, Windows Authenticode, Inno Setup, ASIO hardware or the network updater.
- The existing source carries historical licensing notices; complete a JUCE, ASIO, code-signing and bundled-assets licensing review before distribution.

## Automatic sound-bank restoration from the single ZIP

For convenient one-file delivery, the 2,489 existing WAV files are packed as lossless FLAC with their original WAVE headers preserved. This reduces the download size without changing any audio samples. On the first Windows CMake configure, `scripts/restore_sound_bank.ps1` checks existing WAVs and, if needed, downloads the official **Xiph FLAC 1.5.0 Windows CLI (about 1.3 MB)** from GitHub, validating its published SHA-256 before running it. It then restores the original 2,489 WAV files and verifies the expected SHA-256 of every WAV. No extra manual extraction steps are needed, but internet access is required the first time if `flac.exe` is not already on PATH. The updater does not install the FLAC binary globally.

On Linux portable-engine builds, CMake runs `scripts/restore_sound_bank.sh` using the installed `flac` command. The FLAC CLI is a build-time external tool, not bundled into Veysona or its installer.
