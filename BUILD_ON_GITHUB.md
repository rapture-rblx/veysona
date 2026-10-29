# Windows build from a private GitHub repository

Veysona Studio's GitHub Actions workflow is in `.github/workflows/windows.yml`.

Create a **private** GitHub repository, add the complete extracted project including `.github` and `assets`, and push the main branch. The workflow attempts a Windows C++/JUCE build, VST3 compilation, smoke launch, native tests, and creation of an **unsigned development installer**. This workflow has not been run or verified on Windows in this environment.

Inspect errors under **Actions** before downloading any artifact. A green C++ core test is not evidence that third-party VST3s, ASIO, a microphone or a clean Windows installation work.

**Private source:** Do not make the repo public or attach source ZIPs to public Releases. Confirm that GitHub Actions artifacts remain accessible only to authorized collaborators. Release only binaries and permitted notices when third-party licensing has been settled; see `SOURCE_AND_DISTRIBUTION_POLICY.md`.
