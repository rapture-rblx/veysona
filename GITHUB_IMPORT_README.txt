VEYSONA STUDIO v1.31 - CLEAN GITHUB SOURCE

1. In GitHub Desktop create VeysonaStudio with C++ .gitignore, README and AGPLv3.
2. Extract the CONTENTS of this ZIP directly into that repository folder.
   Do not overwrite the existing LICENSE or README.md.
3. Review the licensing notes below before publishing publicly.
4. Add this to .gitignore if you prefer not to track restored WAVs:
   /assets/samples/
   /assets/instruments/**/*.wav
   /assets/drum_kits/**/*.wav
   /assets/recorded_instruments/
   /assets/recorded_drums/
   (check actual output paths against scripts/restore_sound_bank.ps1)
5. Run build_windows.bat, verify tests, then commit and Publish Repository.

PACKED AUDIO: The original 2,489 WAVs are preserved as FLAC under
assets/packed_soundbank and automatically restored by CMake. The original
recorded piano/guitar/drum packs are optional separate downloads.

LICENSING WARNING: The supplied historical v1.31 source archive had an MIT
LICENSE file, whereas you selected AGPLv3 in GitHub Desktop. This ZIP does
NOT replace your AGPL LICENSE, but that does NOT automatically relicense the
source. Before publishing, confirm ownership/permissions for all code and
assets, and that JUCE/ASIO dependencies meet your chosen licensing terms.
Do not remove legally required copyright or licence notices. The original
MIT licence remains in the full v1.31 archive for reference.

The original full source archive is unchanged.
