#pragma once
#include "AudioCore.h"
#include <cstdint>
#include <vector>
namespace veysona::midi {
// Pure functions keep MIDI editing independent of JUCE and allow regression tests.
double snap(double beat,double gridBeats);
void sanitize(std::vector<Note>& notes,double maximumBeats=1024.);
void quantize(std::vector<Note>& notes,double gridBeats,bool quantizeEnds=false);
// Strength 0 leaves notes untouched; strength 1 quantises precisely to grid.
void quantizeStrength(std::vector<Note>& notes,double gridBeats,float strength);
// Root is a MIDI pitch class (C=0), minor chooses natural minor, chromatic leaves pitch alone.
int nearestScalePitch(int pitch,int rootPitchClass,bool minor);
void constrainToNamedScale(std::vector<Note>& notes,int rootPitchClass,bool minor);
// Velocity shaping with finite/clamped output, fixed amount and exponent.
void velocityCurve(std::vector<Note>& notes,float exponent,float amount=1.f);

void transpose(std::vector<Note>& notes,int semitones);
void shift(std::vector<Note>& notes,double beats);
void duplicate(std::vector<Note>& notes,double offsetBeats);
void legato(std::vector<Note>& notes,double regionEnd);
void scaleVelocity(std::vector<Note>& notes,float factor);
void reverse(std::vector<Note>& notes,double clipLengthBeats);
void constrainToScale(std::vector<Note>& notes,int rootPitchClass,const std::vector<int>& allowedSemitones);
void makeTriads(std::vector<Note>& notes,bool minor);
void applySwing(std::vector<Note>& notes,double gridBeats,double swingPercent);
// Deterministic professional pattern tools. All quantities are musical beats.
std::vector<Note> makeEuclideanRhythm(int steps,int hits,int pitch,double stepBeats,
                                     int rotation=0,float velocity=.85f);
void strumChords(std::vector<Note>& notes,double spreadBeats,bool lowToHigh);
void repeatNotes(std::vector<Note>& notes,int repeats);
void applyProbability(std::vector<Note>& notes,float probability,uint32_t seed);
void velocityRamp(std::vector<Note>& notes,float startVelocity,float endVelocity);
void applyGroove(std::vector<Note>& notes,double gridBeats,
                 const std::vector<double>& timingOffsets);

// MIDI recorder supports overlapping repeated notes and channel-specific note-offs.
struct Recorder {
 struct Active {int pitch=60,channel=1;double beat=0;float velocity=.8f;};
 std::vector<Active> held;
 void noteOn(int pitch,int channel,double beat,float velocity);
 void noteOff(int pitch,int channel,double beat,std::vector<Note>& destination);
 void flush(double beat,std::vector<Note>& destination);
};
void quantizeControllers(std::vector<MidiControllerEvent>& controls,double gridBeats);
void addArpeggio(std::vector<Note>& notes,double stepBeats,int octaves,bool ascending);
void addChordInversions(std::vector<Note>& notes,int inversion);
void humanize(std::vector<Note>& notes,double maxJitterBeats,float velocityVariation,uint32_t seed);
}

namespace veysona::midi {
// Creative MIDI composition utilities: deterministic and independent of the UI.
struct KeyEstimate {int root=0;bool minor=false;double confidence=0;};
KeyEstimate estimateKey(const std::vector<Note>& notes);
// Generates triadic chord regions from scale degrees (1..7) in a selected key.
std::vector<Note> chordProgression(int rootMidi,const std::vector<int>& degrees,
                                  bool minor,double beatsPerChord=4.,float velocity=.75f);
// Simple scale-aware, reproducible bassline generator, following chord roots.
std::vector<Note> basslineFromChords(const std::vector<Note>& chords,double stepBeats,
                                    uint32_t seed=1234);
// Four-on-the-floor style deterministic step sequencer, independent of drum names.
std::vector<Note> drumStepPattern(int bars,int beatsPerBar,uint32_t seed=1234,
                                  double complexity=.5);
}
