# Engineering roadmap — verified systems before feature parity

This document is a release engineering priority queue, not a promise of parity with Ableton Live or FL Studio.

### Gate 1 — Windows compiler and runtime stability (blocking)
- Build Veysona.exe and the five original VST3 bundles using JUCE 8 and Visual Studio 2022.
- Repair all C++/JUCE integration errors from the actual build logs.
- Run a Windows GUI smoke launch, import/play/export tests, external VST3 tests and ASIO/WASAPI device tests.
- Verify packaging/uninstalling on clean Windows VMs, update compatibility and legal licensing.

### Gate 2 — Real-time engine correctness (blocking)
- No allocation, filesystem IO or UI operations on real-time callbacks.
- Measure worst-case callback duration, underruns and thread contention across projects.
- Implement sample-accurate scheduling, plug-in latency compensation, safe bus routing, high-channel-count recording, rollback-on-crash.

### Gate 3 — Editing and workflow
- Full transaction-based undo/redo (not just MIDI and last six audio-edit snapshots).
- Real dockable interface/workspace presets; click-to-quantise tempo changes and automation.
- Multitake recording lanes, comping, clip slip/crossfades, audio warping, sidechains and device racks.

### Gate 4 — Third-party interoperability
- Sandboxed plugin scanning and crash isolation; plug-in latency and parameter-state restoration.
- Investigate CLAP hosting with independent licensing and integration review.
- SFZ multisampling, sample index/search, device mappings and custom MIDI scripting API.

### Gate 5 — Premium creative tooling
- Convolution reverb, dynamics EQ, true-peak lookahead limiter, LUFS meter compliance verification.
- Advanced spectral processing, adaptive pitch/time algorithm, local stem separation and optional AI composition.
- Plugin presets, reusable racks, undoable smart generation, live clip capture, accessibility and profiling.

**What v1.4 actually supplies**: offline audio and MIDI production analysis, tested CLI and source-level GUI commands, enlarged content library. It does **not** satisfy gates 1–5 yet.
