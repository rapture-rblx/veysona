## v1.25 — 2026-10-28
- Transparent silver/blue branding files and native installer art.
- 1,280 newly synthesized WAV samples, 80 effect presets, browser similarity curation of 34 redundant legacy labels.
- General MIDI channel 10 drum support, VST3 note chasing and real-time monitoring while transport is stopped.
- VST3 C4 audition and diagnostics for output device, mute, gain, MIDI and plugin channels.
- 4,096-point live EQ analyzer, interpolated spectrum.
- Resonator, auto-wah, wavefolder and ping-pong delay; portable 40/40 CTest pass.
- Windows GUI and real VST3 compatibility testing still required.

## 1.24.0 — Media compatibility, MIDI workflow, export safeguards
- Automatically emit RF64 WAV for exports beyond 4 GiB; import supported RF64 and WAVE_FORMAT_EXTENSIBLE formats.
- Channel-aware MIDI duplicate filtering, polyphonic legato and repeated-note overlap tools.
- Abort offline export on VST3 processing errors or non-finite plugin output.
- Expanded project health checks and regression suite to 38/38 portable tests.
- Updated Windows release scripts and branding metadata (Windows binaries not yet verified).

## v1.20 — Audio export fidelity and workflow
- Added selectable high-fidelity offline time stretching with finer stereo cross-correlation and heavier grain overlap.
- Switched mixer peak display to calibrated logarithmic dBFS and visible clip indication.
- Added repeatable TPDF dither for fixed-depth WAV exports and preserved optional undithered export at the C++ API level.
- Saved project-owned clips and sampler/drum samples as 32-bit-float WAV to avoid repeated PCM16 precision loss.
- Corrected JUCE range-slider fallback drawing parameters.
- Added dedicated fidelity/export regression suite and updated installer/CI release identifiers.

## v1.15 — Mixer and piano-roll workflow
- Logarithmic mixer faders, pan reset, pan dragging and channel routing/sends popup.
- Exclusively/additively toggled solo, four-bus routing controls and clearer meters.
- Scale-aware note editing in twelve major/minor keys, MIDI strength quantisation and velocity shaping.
- Regression tests for mixer and MIDI utilities; Windows visual QA still pending.

## 1.14.0 — Spectrum EQ 8 and workflow reliability
- Redesigned EQ graph and band strip, expanded editor, presets, clipboard and per-track history.
- Shift-fine drag, Alt-bypass, saved eight-band presets, validated project EQ parameters.
- Track menu limit and history/reorder safeguards; portable EQ workflow regression tests.
- Windows native GUI test status: pending.

## 1.13.0 — UI consistency and menu redesign

- Polished menu bar, top ribbon, browser, arrangement, piano roll, and lower panels.
- Improved grouped and checked menu commands while preserving legacy IDs, key mappings and functionality.
- No audio engine/content removed. Windows JUCE GUI compilation remains unverified.

# v1.9 — Production workspace redesign

- New right-docked library with Sounds/Plug-ins browsing; single-click preview and double-click import.
- Cleaner top transport, enlarged arrangement, dockable/hideable lower editor and show/hide browser.
- Track-level mute, solo and peak displays; persistent musical grid across all tracks.
- Shared tested layout geometry, support for 1200+ px-wide windows, saved browser visibility.
- 1.9.0 build, CI and installer metadata.

See `RELEASE_NOTES_v1.9.md`. Earlier release notes remain available in this archive.

## 1.10.0 — EQ and responsive workspace

- Rebuilt the eight-band parametric EQ UI with real selected-channel FFT analyser, labelled filter curve, individual band enable/mode selection, gain/frequency/Q readout and smoother audio coefficient transitions.
- Introduced an arrangement-header gain dial on each channel, with drag/Shift-fine/wheel/unity-reset interactions.
- Added interactive splitter components for the browser, lower editor and two internal dock columns, with persistent dimensions and View > Reset Workspace Layout.
- Added targeted DSP/geometry tests and Windows EQ/user-interaction QA checklist. This is still developer source without a verified Windows GUI binary.
