# Veysona professional quality gates — v1.24

The user's 9–9.5/10 goals are **targets**, not objectively verified scores.
A release qualifies for a substantiated 9/10 only after native Windows acceptance work, external audio evaluation, and repeatable performance results.

| Area | Evidence currently available | Remaining acceptance work |
|---|---|---|
| UI / branding | Branded assets and layout tests | high-DPI hands-on evaluation and keyboard-only accessibility |
| Audio fidelity / EQ | portable fidelity, dither, EQ tests | blind level-matched renders, reference null tests, frequency/sweep measurements |
| Mixer | routing and automation tests | 100+ track load and click-free automation sweep with latency compensations |
| Piano / MIDI | new channel-aware editing and regression | mouse/keyboard usability, long patterns, expression integration, undo stress |
| Time stretch | WSOLA quality modes and portable tests | transient/tonal stereo artifact metrics vs reference tools at 0.25–4x |
| VST3 | source integrations and exception guards | native compatibility matrix; **out-of-process plugin crash isolation absent** |
| Instruments / FX | samples/presets tests | instrument polyphony/voice stealing and measured CPU/preset usability |
| Performance | sparse arrangement optimization tests | ASIO low-buffer, high-track, long-clip benchmarking and callback lock removal |
| Stability / recovery | continuous recording spool, project save generation, health checks | disk full, device disconnect, power loss, autosave multi-hour Windows soak |
| Production workflows | improved media/MIDI, long export | cancellable async exports, freeze, PDC, media pool, multi-select editing |
| Installer | branded Inno Setup/CI scripts | execute native Windows build + installer QA, code-sign distribution |
| Licensing | private-source transition policy | verify contributor rights and JUCE commercial/third-party obligations |
