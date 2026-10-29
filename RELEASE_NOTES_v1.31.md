# Veysona Studio v1.31 — verified Windows auto-update groundwork

## New
- Startup splash now shows update-check status when a release channel has been configured.
- Background PowerShell 5.1+ updater helper checks a HTTPS JSON manifest and compares semantic version numbers.
- Help → Check for Updates manual command.
- Optional download and install workflow for a newer **Authenticode-signed** Windows Inno Setup release.
- The downloaded installer is validated using its published SHA-256, expected length, Windows signature trust and a pinned publisher certificate thumbprint.
- Installer is launched only after Veysona closes, with confirmation that user projects should be saved first.
- `scripts/prepare_update_release.ps1` creates a stable manifest from a verified signed Windows installer.
- `PUBLISH_UPDATES_v1.31.md` documents the complete release-feed, signing and publishing workflow.
- The full v1.30 sample/instrument library, recorded-instrument downloader, mixer, MIDI editing and drums remain intact.

## Validation
This source release adds `v131_update_policy` to the portable CTest suite. Run CMake Release and CTest before distribution. The Windows updater requires an actual Windows signing certificate, hosted release feed and native smoke test; this Linux portable build does not prove the Windows update installation flow.

## Rollout
Automatic installation is **disabled until a trusted publisher certificate thumbprint is configured**. The built-in splash and help command can still check for updates when a manifest URL is configured. No manifest endpoint or signing certificate has been invented.

## Single source ZIP
All 2,489 existing WAV samples and instruments are losslessly FLAC-packed inside the one source ZIP. The native build scripts restore their original byte-for-byte WAV content before compilation and verify each file with SHA-256. All effects, loops, core source and test content remain included. No commercial audio bank is downloaded to replace bundled files.
