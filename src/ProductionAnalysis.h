#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "AudioCore.h"

namespace veysona::production {
// Offline analysis only. Do not call these from an audio callback.
struct LoopAnalysis {
 double estimatedBpm=0.0, confidence=0.0;
 double peakDbfs=-120.0, rmsDbfs=-120.0;
 double approximateLufs=-120.0, estimatedTruePeakDbfs=-120.0;
 double spectralCentroidHz=0.0;
 // +1 mono-compatible, -1 phase-inverted, 0 decorrelated; silence returns 0.
 double stereoCorrelation=0.0, channelBalanceDb=0.0;
 std::vector<double> transientsSeconds;
};
// Uses 10 ms energy-onset envelope autocorrelation; silence returns bpm=0.
LoopAnalysis analyseLoop(const std::vector<float>& stereo,uint32_t sampleRate,
                         double minBpm=70.,double maxBpm=190.);
// Envelope-difference peak picker with minimum separation and adaptive threshold.
std::vector<double> detectTransients(const std::vector<float>& stereo,uint32_t sampleRate,
                                     double sensitivity=1.4,double minSeparationSeconds=.08);
// High-quality OFFLINE band-limited windowed-sinc converter (64-tap at default).
std::vector<float> resampleSinc(const std::vector<float>& stereo,uint32_t inputRate,
                                uint32_t outputRate,int taps=64);
// Approximately gated K-weighted loudness, inspired by BS.1770 / EBU R128;
// not a certified compliance meter. Absolute gate -70, relative gate -10.
double integratedLoudnessApprox(const std::vector<float>& stereo,uint32_t sampleRate);
// Estimate intersample peaks using oversampled, band-limited interpolation.
double estimatedTruePeakDb(const std::vector<float>& stereo,uint32_t sampleRate,int oversample=4);
// Offline mastering: target loudness followed by stereo-linked sample and
// interpolated-peak protection. Not a replacement for a mastering limiter.
struct MasteringReport {double inputLufs=-120.,outputLufs=-120.,inputTruePeak=-120.,outputTruePeak=-120., appliedGainDb=0.;};
MasteringReport normalizeLoudnessOffline(std::vector<float>& stereo,uint32_t rate,
                                         double targetLufs=-14.,double ceilingDb=-1.);
// Returns audio content copied into project-owned slices; preserves alignment.
std::vector<Clip> sliceClipAtTransients(const Clip& clip,double sensitivity=1.4,
                                       double minSliceSeconds=.10,size_t maxSlices=128,double projectBpm=120.);
}
