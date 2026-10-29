# Veysona Studio 1.5 — Windows installer

## Release structure

The source tree now contains a complete **Inno Setup 6** project at `installer/Veysona.iss`, a Windows staging script at `scripts/build_installer.ps1`, and a GitHub Actions Windows build workflow. It is **not a prebuilt setup executable**. An actual Windows compiler and JUCE source must first produce `Veysona.exe` and the five Veysona VST3 bundles.

The installer uses the generated Veysona equaliser logo in its taskbar icon and in the wizard. Installing the DAW also installs the full `assets/` tree, including its own sample library, instrument presets, MIDI patterns, demos and branding. By default the installer installs for all users to `Program Files\Veysona Studio`. Optional setup tasks allow a desktop shortcut and installation of the five original `.vst3` bundles in `Program Files\Common Files\VST3`. The uninstaller removes files it installed but leaves user project files untouched.

## Build instructions for maintainers

On Windows 10/11 x64, install:

1. Visual Studio 2022, **Desktop development with C++** (MSVC and Windows SDK).
2. CMake 3.22+ and Git, available in PATH.
3. Inno Setup 6 (e.g. `choco install innosetup -y` in elevated PowerShell).
4. Internet access to fetch the JUCE 8.0.10 tag unless it has already been cached.

From the project root:

```bat
build_release_windows.cmd
```

That command configures and builds the JUCE app plus bundled VST3 projects, runs CTest, stages all required files and invokes the Inno Setup compiler. It **fails rather than producing an installer if anything is missing or the core tests fail**. On success find:

- `dist\Veysona-Studio-Setup-1.5.0.exe` — branded full installer (unsigned)
- `dist\Veysona-Studio-1.5.0-Portable-Windows-x64.zip` — portable application and VST3s

For an existing successful Windows build, run:

```powershell
.\scripts\build_installer.ps1 -NoBuild
```

## GitHub Actions

After publishing the source to an actual GitHub repository, `.github/workflows/windows.yml` attempts the same Windows build, runs all tests, performs a Windows application launch smoke test, compiles the installer and uploads both distribution files as **artifacts**. A build artifact is not a tested public release. Manual QA and distribution-licensing checks are required before public distribution.

## Verification before public distribution

- Install on a clean Windows 10 x64 and Windows 11 x64 machine.
- Launch both portable and installed builds, select WASAPI and, if available, ASIO devices.
- Import and export several WAV files, record audio and MIDI, test mute/solo, samples, Session View and save/reopen a project.
- Load each bundled VST3, plus at least two independently developed third-party VST3 plugins.
- Reopen projects containing sampler audio, automation and VST3 state.
- Verify uninstallation removes only the installer-created files, not user projects.
- Review AGPL/JUCE licensing and third-party notices (see `DISTRIBUTION_NOTICE.md`).
- Digitally sign `.exe` installers for public distribution; unsigned setup programs may be flagged by Windows SmartScreen.

## Known limitations

This release is not yet certified as an error-free DAW. The JUCE GUI and VST3 build cannot be compiled or runtime-tested in the current Linux environment. The audio callback still uses a try-lock and may briefly output silence during edits; plugin crash isolation and automatic latency compensation are incomplete. Expect the Windows build to reveal additional integration fixes before a stable release.
