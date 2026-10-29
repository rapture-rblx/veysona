# Veysona Audio Tools — reproducible offline workflow

This C++ command-line utility does not require JUCE and is compiled as `veysona_audio_tool` (`veysona_audio_tool.exe` on Windows). It demonstrates production processing independently of the graphical application.

Build with CMake using `-DVEYSONA_BUILD_GUI=OFF`, then:

```powershell
.\build\Release\veysona_audio_tool.exe analyze "assets\samples\Loop - Pulse House 120BPM.wav"
.\build\Release\veysona_audio_tool.exe master "input.wav" "master.wav" -14 -1
.\build\Release\veysona_audio_tool.exe resample "input.wav" "44k.wav" 44100
mkdir slices
.\build\Release\veysona_audio_tool.exe slice "drum_loop.wav" "slices"
.\build\Release\veysona_audio_tool.exe chords "chords.mid" 60 120
.\build\Release\veysona_audio_tool.exe drums "drums.mid" 120 1234
```

On Linux/macOS, the executable is normally `./build/veysona_audio_tool`, not under `build/Release/`.

`analyze` prints estimates; `master` writes 24-bit stereo PCM, `resample` uses a band-limited offline converter, `slice` writes individual 24-bit PCM WAV files, and the MIDI commands create standard type-0 MIDI files. The utility rejects writing over its input audio file and refuses to overwrite existing slice files. Back up important source material regardless.

**Accuracy caveat**: LUFS and dBTP are approximations, and peak-constrained mastering may not achieve the requested target loudness. This is not a certified broadcast mastering tool or realtime plugin.


## Added in v1.7

Experimental **monophonic** audio-to-MIDI from a clean isolated sound (does not handle chords or vocal tuning):

```bash
veysona_audio_tool transcribe clean_melody.wav extracted.mid 120
```

Render a short editable-song-template preview using the native C++ synthesiser and 24-bit WAV export:

```bash
veysona_audio_tool template house house.wav 12
veysona_audio_tool template dnb dnb.wav 12
veysona_audio_tool template hiphop hiphop.wav 12
veysona_audio_tool template ambient ambient.wav 12
```

All inputs must be valid WAV files or specified output paths. These operations run **offline** and do not demonstrate real-time VST3 or GUI behaviour.

## Time warp and pitch tools (v1.16)

```
veysona_audio_tool stretch audio.wav stretched.wav 0.75 smooth
veysona_audio_tool stretch drums.wav faster.wav 1.5 drums
veysona_audio_tool fit loop.wav fourbars.wav 16 120
veysona_audio_tool tempo old.wav new.wav 100 128
veysona_audio_tool pitch vocals.wav shifted.wav -3
veysona_audio_tool warp drums.wav quantized.wav 120 0.5
```

Stretch speed 0.25–4.0; original tonal pitch is approximately preserved. Fit uses beats not bars (e.g. 16 beats = four 4/4 bars). Pitch -12 to +12 semitones approximately preserves duration, but does not preserve formants. Warp requires reliable transients. New files are PCM24 WAV. Do not supply the same input/output file path.
