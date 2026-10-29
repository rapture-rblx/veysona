# Veysona v1.29 incremental effects and instruments expansion

Apply to the v1.28 reconstructed source with stereo diagnostics. This package contains replacement versions of `src/AudioCore.h`, `src/AudioCore.cpp`, `src/Main.cpp` and two updated tests. It also adds 96 original synthesized playable C4 instrument WAVs and a reproducible inventory manifest. Extract into the existing Veysona project folder, preserving directory structure.

## Added DSP algorithms
1. Asymmetric diode drive (independent asymmetric nonlinear waveshaping)
2. Octave fuzz (full-wave nonlinear overtone generation)
3. Headphone stereo crossfeed (left/right channel mixing)

Each is available from the Creative Effects menu, defaults to bypassed, and is saved/loaded with project state. These are lightweight creative processors, not claims of studio-grade analog modelling or true-peak mastering.

## Added instruments
96 original synthesized sample instruments across eight timbral families (12 variations each): FM Bell, Analog Brass, Glass Pluck, Warm Pad, Reed Organ, Bowed Texture, Mallet, Digital Keys. They are sample instruments for MIDI note triggering; not 96 new synthesizer engines. Existing 360 instrument WAVs are preserved, bringing the total to 456.

## Verification
C++ portable build succeeded and 43/43 tests passed, including effect audio-difference, finite-output and new instrument WAV-read tests. Windows GUI and audio-driver testing not performed here. The 96 files have unique PCM SHA-256 hashes; this does not establish perceptual uniqueness.
