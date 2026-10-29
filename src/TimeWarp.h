#pragma once
#include "AudioCore.h"
#include <cstddef>
#include <vector>

namespace veysona::warp {
// All operations are OFFLINE and operate on project-owned audio, never the source WAV.
// A value < 1.0 slows playback down; > 1.0 speeds playback up.
enum class Quality { Smooth, Percussive, HighFidelity };
struct Marker { double sourceSeconds=0, targetSeconds=0; };
// HighFidelity spends extra offline CPU on longer, overlapping correlation windows.
// Percussive prioritises transient timing; Smooth is the default quick render.
struct Options { Quality quality=Quality::Smooth; double searchMs=10.; };

// Band-limited offline pitch resampling. Ratio >1 raises pitch and shortens
// audio; it rejects frequencies that would alias after decimation.
std::vector<float> resamplePitchBandlimited(const std::vector<float>& stereo,double playbackRateFactor);
std::vector<float> stretchWsola(const std::vector<float>& stereo, uint32_t sampleRate,
                                double speed, Options options={});
void stretchClip(Clip& clip,double speed,Options options={});
void fitClipToBeats(Clip& clip,double beats,double projectBpm,Options options={});
void pitchShiftClip(Clip& clip,double semitones,Options options={});
// Piecewise time mapping with ordered source/target points including both endpoints.
void warpClipToMarkers(Clip& clip,const std::vector<Marker>& markers,Options options={});
// Generate percussive marker proposals at nearby musical beat subdivisions.
std::vector<Marker> suggestBeatMarkers(const Clip& clip,double projectBpm,
                                      double subdivisionBeats=.5,size_t limit=32);
// Position the right clip's onset before the left clip ends and set equal-power fades.
// Returns the actual overlap (s), which can be shorter than requested.
double crossfadeAdjacent(Clip& left,Clip& right,double bpm,double requestedSeconds);
}
