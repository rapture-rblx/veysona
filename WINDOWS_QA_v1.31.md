# Veysona Studio v1.31 — Windows native acceptance checklist

## Prerequisites
- [ ] Build and run JUCE 8 GUI on clean Windows 10/11 x64, at 100%, 125% and 150% DPI.
- [ ] Confirm transparent Veysona logo in splash, app header, EXE/taskbar icon and installer.
- [ ] Confirm existing audio, MIDI and drum-step workflows are unchanged.
- [ ] Confirm 47/47 portable regression tests pass on Windows in Release mode.
- [ ] Test WASAPI and optional licensed ASIO driver at 44.1 kHz and 48 kHz.

## Release channel (set before distribution)
- [ ] Configure a real HTTPS feed URL in CMake (`VEYSONA_UPDATE_MANIFEST_URL`).
- [ ] Sign the Inno Setup installer with a certificate trusted by Windows.
- [ ] Configure the 40-character signing-certificate thumbprint in CMake.
- [ ] Build and install an older version with the *same* signing certificate and release channel.

## Update acceptance
- [ ] Splash says "Checking for updates..." without delaying audio initialization.
- [ ] Offline/no feed: Veysona continues to start; failure is nonfatal.
- [ ] Up-to-date feed: visible "You're up to date" status.
- [ ] Newer version: notification shows the version and release notes.
- [ ] Help → Check for Updates works on demand.
- [ ] Installer download runs in background, showing progress state in main app status.
- [ ] Wrong hash is rejected and does not run.
- [ ] Unsigned installer is rejected and does not run.
- [ ] Installer with untrusted/wrong signer thumbprint is rejected and does not run.
- [ ] HTTPS → HTTP redirect is rejected.
- [ ] Truncated installer and non-EXE data are rejected.
- [ ] User can defer installation without losing their project.
- [ ] User can save project, choose Install & Restart; app closes before Setup runs.
- [ ] Inno Setup upgrades rather than creates a duplicate installation (same AppId).
- [ ] Project files, settings and the separately installed CC0 recordings are retained.
- [ ] App relaunches into the updated version after successful installation.
- [ ] Non-admin account handles the installer UAC prompt correctly.
- [ ] Interrupted download or disk exhaustion does not delete the working installation.

The native Windows updater is **not verified** by the Linux C++ test suite. No channel is live until its real HTTPS release manifest is published.
