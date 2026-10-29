# Publishing Veysona Studio updates — v1.31

## What users see

When Veysona opens, the splash screen displays **Checking for updates...** while a separate Windows process checks a small JSON feed. Normal startup continues even if the network is unavailable. If a newer version is found, Veysona shows its version and release notes. **Help → Check for Updates** also works whenever the user wants to check manually.

If the build has a pinned Windows Authenticode publisher certificate, the user can choose **Download & Install**. The updater downloads the Inno Setup EXE into their per-user local application-data folder, verifies the advertised file size, SHA-256 digest, trusted Authenticode signature and exact certificate thumbprint, and then offers **Install & Restart**. Veysona must exit before Setup runs. Setup must retain the same `AppId` to upgrade the existing installation. The helper may need a Windows UAC approval.

If no publisher certificate is configured, **automatic installation is disabled**. Users can follow the official release link and install manually. Unsigned or mismatched installers are **never** automatically executed. This is deliberate and is not a build error.

## Step 1 — Choose one public HTTPS release feed

You can keep the **source proprietary**. Host the Windows **binary releases** and the update manifest in a separate public GitHub repository (e.g. `your-org/veysona-releases`) or on your own HTTPS website. Do not publish the source ZIP if you want it closed-source.

A GitHub arrangement:

- Create a public repository for release assets. Your source repository may remain private.
- Host the manifest at `https://raw.githubusercontent.com/YOUR_ORG/YOUR_RELEASE_REPO/main/updates/stable.json`.
- Upload signed installer files as GitHub **Release assets**, e.g. `https://github.com/YOUR_ORG/YOUR_RELEASE_REPO/releases/download/v1.32.0/Veysona-Studio-Setup-1.32.0.exe`.
- Upload a new installer for every release, keeping prior releases available. GitHub's public Releases are readable without a login.

The account/repository placeholders above are **examples**, not a real feed. Never publish a feed containing imaginary or unverified download links.

## Step 2 — Code-sign your Windows installer

Get a Windows code-signing certificate issued to the legal publisher. Sign the final Inno Setup EXE after it is built, and timestamp the signature. Example:

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a "dist\Veysona-Studio-Setup-1.32.0.exe"
Get-AuthenticodeSignature "dist\Veysona-Studio-Setup-1.32.0.exe" | Format-List Status,SignerCertificate
```

Copy the signer certificate's **40-digit Thumbprint** from the `SignerCertificate` entry. Check the signature status is `Valid` on a clean Windows machine. Store the signing private key securely, not in the repository. If you change the signing certificate, you must plan a trusted signer transition; do not allow an unsigned manifest to change the pinned publisher automatically.

## Step 3 — Compile Veysona with your channel settings

From the source folder, replace the placeholder URL and thumbprint with your *real* values:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DVEYSONA_BUILD_GUI=ON -DVEYSONA_BUILD_BUNDLED_VST3=ON `
  -DVEYSONA_UPDATE_MANIFEST_URL="https://raw.githubusercontent.com/YOUR_ORG/YOUR_RELEASE_REPO/main/updates/stable.json" `
  -DVEYSONA_UPDATE_SIGNER_THUMBPRINT="YOUR_ACTUAL_40_HEX_CERTIFICATE_THUMBPRINT"
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
./scripts/build_installer.ps1 -NoBuild
```

The manifest URL and trusted certificate thumbprint are **embedded at compile time** to prevent an ordinary preference-file modification from redirecting updates. For CI builds, set the two CMake variables through your trusted build environment. The signing thumbprint is public; the certificate **private key** is secret.

If you haven't acquired signing yet, you can set only `VEYSONA_UPDATE_MANIFEST_URL`; startup checks and links work, but automatic installation stays disabled.

## Step 4 — Publish your next update

1. Build and test the new version on Windows, including your VST3 plugins and WASAPI/ASIO hardware where applicable.
2. Compile `Veysona-Studio-Setup-X.Y.Z.exe` using `scripts/build_installer.ps1`.
3. Authenticode-sign the **finished Setup EXE** and verify it.
4. Create a matching GitHub Release and upload that exact signed EXE as a release asset.
5. Generate `updates/stable.json` **from the signed local EXE**, for example:

```powershell
./scripts/prepare_update_release.ps1 `
  -Installer "dist/Veysona-Studio-Setup-1.32.0.exe" `
  -Version "1.32.0" `
  -DownloadUrl "https://github.com/YOUR_ORG/YOUR_RELEASE_REPO/releases/download/v1.32.0/Veysona-Studio-Setup-1.32.0.exe" `
  -ReleasePage "https://github.com/YOUR_ORG/YOUR_RELEASE_REPO/releases/tag/v1.32.0" `
  -PublisherThumbprint "YOUR_ACTUAL_40_HEX_CERTIFICATE_THUMBPRINT" `
  -ReleaseNotes "New instruments and stability fixes"
```

6. Review and commit the generated `updates/stable.json` to the public release-feed repository.
7. Verify the public URLs work **without authentication**, then test updates from the previous installed version on Windows. Publish the stable manifest **last**, after every asset has finished uploading.

Example manifest format (the hash and URL below are **placeholders**, not a usable published update):

```json
{
  "version": "1.32.0",
  "installer_url": "https://github.com/YOUR_ORG/YOUR_RELEASE_REPO/releases/download/v1.32.0/Veysona-Studio-Setup-1.32.0.exe",
  "sha256": "INSERT_ACTUAL_64_CHARACTER_SHA256_FROM_SIGNED_EXE",
  "size_bytes": 123456789,
  "release_page": "https://github.com/YOUR_ORG/YOUR_RELEASE_REPO/releases/tag/v1.32.0",
  "release_notes": "New effects and stability updates"
}
```

## Safety and compatibility limitations

- Never auto-install from a manifest whose URL isn't HTTPS, or an EXE whose SHA-256 or Authenticode signature doesn't verify.
- Automatic upgrades require an **installed, signed Inno Setup** application. A source ZIP is **not** a suitable Windows update payload.
- The updater never modifies a project file or instrument library in place. Existing `%APPDATA%` acoustic recordings and the user's projects are retained.
- The user is prompted before restart so unsaved work can be saved. The UI does **not** silently close an editing session.
- Only **Windows x64** currently has the installer and updater helper. Linux is supported for the portable engine tests only.
- Do a Windows update smoke test from old version → new version before notifying real users. Test elevation, low storage, interrupted network connections, and publisher certificate mismatches.
- The v1.31 package contains updater **source**, not an already live update channel. No remote updates will appear until you actually publish and configure the feed.
