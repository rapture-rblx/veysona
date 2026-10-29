#pragma once
#include "AudioCore.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace veysona {
// Quantised performance grid: every row is a track, and its audio/MIDI clips are
// launchable slots. This is independent from arrangement playback. GUI and audio
// threads must coordinate edits/launch commands externally (e.g. the JUCE lock).
enum class LaunchKind { Stop, Audio, Midi };
struct LaunchSlot {
 LaunchKind kind=LaunchKind::Stop;
 int index=-1; // Index into the selected track's audio clips or MIDI regions.
 double beganAtBeat=0;
};
struct PerformanceTrackState {
 LaunchSlot active{}, queued{};
 bool hasQueue=false;
 double queuedForBeat=0;
};
enum class PerformanceMidiType { NoteOn, NoteOff, CC, PitchWheel, Pressure, AllNotesOff };
struct PerformanceMidiEvent {
 PerformanceMidiType type=PerformanceMidiType::NoteOn;
 int offset=0, channel=1, number=0, value=0;
};
// Copy a scene into the linear arrangement as looping audio/MIDI for a fixed
// musical span. Not a live capture recorder: edits source project offline.
// Returns number of tracks for which an arrangement clip was created.
std::size_t commitPerformanceScene(Session& session,std::size_t scene,double startBeat,double lengthBeats);
class PerformanceLauncher {
 std::vector<PerformanceTrackState> tracks_;
 double quantisationBeats_=4.;
public:
 void prepare(std::size_t numberOfTracks);
 void clear() noexcept;
 void setQuantisation(double beats);
 double quantisation() const noexcept { return quantisationBeats_; }
 bool isRunning() const noexcept;
 const PerformanceTrackState& state(std::size_t track) const;
 // Schedules at next grid boundary; exactly-on-boundary launches immediately.
 void queue(std::size_t track,LaunchKind kind,int slot,double currentBeat);
 void queueScene(const Session& session,std::size_t scene,double currentBeat);
 void stopAll(double currentBeat);
 // Called on the audio thread; renders a stereo track of triggered looping clips.
 // Does not process per-track EQ/plugins/route — caller does that in usual chain.
 void renderTrack(const Session& session,std::size_t track,int64_t samplePosition,
                  int count,float* left,float* right,bool builtInMidi=true);
 // Generate MIDI for hosted VST3 instruments, including queued clip transitions.
 // Caller supplies scratch storage; this method never allocates.
 int collectMidiEvents(const Session& session,std::size_t track,int64_t samplePosition,
                       int frames,PerformanceMidiEvent* output,int capacity) const;
};
}
