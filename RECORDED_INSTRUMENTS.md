# Veysona Studio v1.30 — Genuine recorded instruments (CC0)

The recorded-instrument installer **does not generate synthesised imitations**. It installs audio sampled from physical piano, acoustic guitars and an acoustic drum kit. These recorded packs are optional separate downloads, **not yet included in the source ZIP**.

## Install on Windows

1. Install Python 3 and FFmpeg if needed. For FFmpeg, run `winget install Gyan.FFmpeg` and reopen Terminal.
2. Open a terminal at the Veysona Studio project root.
3. Run `py -3 tools/install_recorded_instruments.py --quality compact`.
4. Restart Veysona. Use **Load Instrument → Piano instruments (recorded)** or **Guitar instruments (recorded)**, or **Drum Rack → Recorded Acoustic / Soft / Hard**.
5. To check WAV integrity later: `py -3 tools/install_recorded_instruments.py --verify-only`.

The compact download retrieves around 90 MB of OGG recordings plus small drum crates. **On Windows, downloaded WAVs are stored under `%APPDATA%\Veysona Studio\Recorded Assets`** so no administrator privileges are needed, even if the application is installed in Program Files. The full source-quality option downloads around 1.8 GB of original WAV recordings: `py -3 tools/install_recorded_instruments.py --quality full`. Both modes convert selected pitches/hits to stereo PCM16 44.1kHz WAV (not synthesized). Instrument mapping includes **32 recorded note/velocity files for piano**, **27 files each for nylon and steel guitar**, and **three authentic acoustic drum kit variants with eight recorded hits each**, chosen from professionally multisampled source libraries. Veysona switches the actual recorded take according to MIDI pitch and velocity (four piano dynamics and three guitar dynamics). Veysona pitches each zone between its recorded roots.

## Sources and permissions

All new recordings are from sources declaring **Creative Commons CC0 1.0**:

- **Meta Open Sound — Meta Piano:** https://github.com/facebookincubator/Meta-open-sound/releases/tag/meta-piano-v1.0.0 (88-key recorded upright piano, four velocity layers).
- **Meta Open Sound — Meta Nylon Guitar:** https://github.com/facebookincubator/Meta-open-sound/releases/tag/meta-nylon-guitar-v1.0.0
- **Meta Open Sound — Meta Steel Guitar:** https://github.com/facebookincubator/Meta-open-sound/releases/tag/meta-steel-guitar-v1.0.0
- **Versilian Studios Virtuosity Drums**, via ferrosintesis packaged acoustic recordings:
  https://github.com/0x4D44/ferrosintesis/tree/main/crates/ferrosintesis-samples-drumkit and
  https://github.com/0x4D44/ferrosintesis/tree/main/crates/ferrosintesis-samples-drumkit2

The exact GitHub Meta release SHA-256 digests are pinned in the installer script. The install generates an `RECORDED_SOURCE.json` manifest for each playable pack with individual WAV checksums. No generated sound is mislabeled as a recorded sample.

## Drum sequencer

Selecting a Drum Rack MIDI track automatically switches the piano roll into an 8-lane 16/32-step drum sequencer, preserving its 4-bar MIDI clip. Click a step to toggle, Shift-click to accent, right-click to erase, and drag in the velocity lane to edit hit intensity. Click pad names to audition the recordings. Other MIDI tracks still use the piano roll.

**Important:** The existing 456 general MIDI/sampled instrument entries from earlier releases are not all verified acoustic recordings. The new *recorded* collection is separate and identified explicitly. The app will not invent drum-kit hits for the new recorded Drum Racks when WAVs are missing; install the genuine recordings first.

## What's included in the optional recorded download

- **Recorded upright piano:** 32 WAV files across 8 keys and four different actual recorded velocities.
- **Recorded nylon guitar:** 27 WAV files across 9 keys and three recorded articulations/velocities.
- **Recorded steel-string guitar:** 27 WAV files across 9 keys and three recorded velocities.
- **Three recorded drum-kit variations:** Soft, natural and hard, eight physical drum/cymbal recordings each, 24 real WAV hits.

**Total: 110 acoustic recordings once the installer is run**. There are no samples in the update ZIP because original recordings must be downloaded from the licensed primary sources. The new programs appear in Veysona after installation. This does not retroactively make any older synthesized sample bank an acoustic recording.
