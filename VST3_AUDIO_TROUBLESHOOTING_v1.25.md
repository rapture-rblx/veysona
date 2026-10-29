# Can't hear your VST3? — Veysona Studio v1.25

1. Build or install v1.25, then open **Audio Settings** and choose your actual speakers/headphones (WASAPI or ASIO) as the output. Confirm Windows audio is not muted and Veysona's master gain is not zero.
2. Select the track that contains the plugin. Open **Plugin Editor** and choose **VST3 audio diagnostics**. Confirm the plugin loaded, its channel count, track mute/gain and selected output.
3. If this is a **VST3 synth/instrument**, choose **Plugin Editor → Test VST3 instrument (C4)**. This sends one C4 note to the selected instrument. The transport may be stopped. Ensure the plugin has an audible preset and does not require a missing external sound library.
4. If you have a USB MIDI controller, choose **MIDI Arm**. In v1.25 it enables monitoring while stopped and recording while playing. The selected VST3 track receives keyboard MIDI directly, including note-off.
5. If this is a **VST3 effect** (EQ, compressor, delay, reverb), it won't produce a tone from silence. Load a WAV onto the same track and press Play. The C4 test correctly reports that it isn't a sound-generating instrument.
6. If you opened an older project, restore its plugin chains explicitly using **Settings → Restore Saved VST3 Chains**. Saved plugin file locations are not automatically executed on project load.
7. Confirm no other track is soloed and the selected plugin track is not muted. Open another working instrument or the bundled Veysona Prism instrument to distinguish plugin-specific failure from the audio output configuration.
8. If a plugin reports a processing fault, save the project and relaunch the session. In-process VST3 failures can't always be recovered safely; test problematic plugins separately.

These checks were written from source analysis. Native Windows, actual WASAPI/ASIO devices and third-party VST3 plugins have **not** been tested in this environment. If C4 still makes no sound after this, provide the plugin name/version, audio device and the diagnostics text so the specific failure can be reproduced.
