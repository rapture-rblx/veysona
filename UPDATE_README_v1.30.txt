VEYSONA STUDIO v1.30 — REAL INSTRUMENTS + DRUM SEQUENCER UPDATE
===============================================================

INSTALL THIS ZIP:
- Back up your existing v1.29 source directory.
- Extract the contents of this ZIP into that existing source directory,
  preserving the `src`, `tools`, `tests`, `assets`, `scripts`, and `installer`
  paths. Allow source files with matching names to be replaced.
- Rebuild the Windows app using the project build instructions.

RECORDED INSTRUMENT AUDIO IS A SEPARATE INSTALL (not inside this ZIP):
1) Install Python 3 and FFmpeg (winget install Gyan.FFmpeg).
2) Run Install_Recorded_Instruments.cmd on Windows in the rebuilt project
   (also available in the packaged application).
3) The installer downloads official CC0 recorded acoustic instrument packs,
   validates checksums, converts to standard WAV, and installs 110 WAV files
   to %%APPDATA%%\Veysona Studio\Recorded Assets.
4) Restart the DAW. New Piano/Guitar recorded presets appear under
   Load Instrument. Drum Rack offers Recorded Acoustic, Soft and Hard.
5) To check recordings, run:
   py -3 tools/install_recorded_instruments.py --verify-only

- 32 recorded upright piano samples (8 roots x 4 recorded velocity layers)
- 27 recorded nylon guitar samples (9 roots x 3 recorded velocity layers)
- 27 recorded steel guitar samples (9 roots x 3 recorded velocity layers)
- 24 recorded acoustic drum hits across three kits (8 each)

DRUM SEQUENCER:
Select a Drum Rack track: Piano Roll automatically becomes 8 named drum
lanes with a 16/32-step editor, note toggle, shift accent, velocity editing
and pad audition. Normal MIDI tracks retain the piano roll.

IMPORTANT VERIFICATION:
46/46 portable C++/Python tests pass. Native JUCE Windows GUI, VST3/ASIO,
actual acoustic downloads and installer execution have not been tested here.
This is an incremental source update, not a compiled Windows application.

LICENSING:
All new recorded packs reference Meta Open Sound / Virtuosity Drums CC0
source recordings. Existing historical Veysona samples may have different
rights; this update does not make the entire DAW or all old content CC0.
