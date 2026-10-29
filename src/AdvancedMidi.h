#pragma once
#include "AudioCore.h"
#include <cstdint>
#include <vector>
namespace veysona::advanced_midi {
using Notes=std::vector<Note>;
void gate(Notes& notes,double factor);
void shiftOctaves(Notes& notes,int octaves);
void invertPitches(Notes& notes,int pivot);
void mirrorTime(Notes& notes,double regionBeats);
void removeDuplicates(Notes& notes,double toleranceBeats=.00001);
void limitPolyphony(Notes& notes,int maxVoices);
void setFixedVelocity(Notes& notes,float velocity);
void compressVelocities(Notes& notes,float centre,float ratio);
void scaleDurations(Notes& notes,double factor,double minimum=.01);
void stagger(Notes& notes,double intervalBeats);
void rotatePitches(Notes& notes,int positions);
void trimToRegion(Notes& notes,double lengthBeats);
void accentEvery(Notes& notes,int interval,float boost);
void snapDurations(Notes& notes,double gridBeats);
void makeStaccato(Notes& notes,double maxDurationBeats);
void makeLegato(Notes& notes,double maxLengthBeats);
// Sustain each voice until its next note of the same pitch AND MIDI channel.
// Unlike monophonic legato, chords retain their independently held voices.
void makePolyphonicLegato(Notes& notes,double maximumBeats=8.);
// Prevent same-pitch MIDI retriggers from overlapping and causing stuck notes.
void resolveSameVoiceOverlaps(Notes& notes,double minimumBeats=.001);
}
