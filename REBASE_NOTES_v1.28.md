# Reconciling v1.28 with an existing v1.27 checkout

**Do not replace an existing working v1.27 project with the reconstructed full source without comparing them first.**

1. Save a copy of the existing v1.27 source and build settings.
2. Compare these files against v1.28, taking the original v1.27 file as authoritative where a feature differs:
   - `src/AudioCore.h`, `src/AudioCore.cpp` — eight-bus routing and solo-state hint
   - `src/Main.cpp` — return strips, ASIO settings, blue/silver look, startup splash
   - `CMakeLists.txt` — optional ASIO SDK switch, release metadata
   - `installer/Veysona.iss`, `.github/workflows/windows.yml`, `scripts/build_installer.ps1` — packaging
3. Retain the v1.27 audio, MIDI, plugin and UI work that is not present in v1.28.
4. Run the portable test suite and the Windows QA checklist from this branch.

All original v1.25 content from the three archives has been reassembled in this full source ZIP, so the music library should not be reduced.
