#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <cstddef>
#include <optional>

namespace veysona {
constexpr double kPi = 3.14159265358979323846;
enum class WavFormat { PCM16, PCM24, Float32 };
// TPDF decorrelates fixed-depth quantisation. Floating-point WAV is never dithered.
enum class DitherMode { None, TPDF };
// Bounded-memory offline WAV output. Append arbitrary-sized interleaved stereo
// blocks; the dithering stream is invariant to block segmentation.
// Uses classic RIFF/WAV (<= 4 GiB), not RF64.
class WavStreamWriter {
public:
 WavStreamWriter(const std::string& path,uint32_t rate,WavFormat format,
                 size_t expectedFrames,DitherMode dither=DitherMode::TPDF);
 WavStreamWriter(const WavStreamWriter&)=delete;
 WavStreamWriter& operator=(const WavStreamWriter&)=delete;
 void append(const float* interleavedStereo,size_t frames);
 void finish();
 ~WavStreamWriter();
private:
 std::ofstream out_;
 size_t expectedFrames_=0,writtenFrames_=0;
 WavFormat format_=WavFormat::PCM16;
 DitherMode dither_=DitherMode::TPDF;
 uint32_t rng_=0x9e3779b9u;
 double random01();
};
enum class Instrument { Sine, Saw, Square, Triangle, Noise, Sampler, DrumRack, Wavetable };
struct Note { int pitch=60; double beat=0, length=1; float velocity=.8f; int channel=1; };
enum class MidiControlType { CC=0, PitchWheel=1, ChannelPressure=2 };
struct MidiControllerEvent {
 MidiControlType type=MidiControlType::CC;
 int number=1, value=64, channel=1;
 double beat=0;
};
struct MidiRegion {
 std::string name="MIDI Clip";
 double startBeat=0, lengthBeats=4;
 std::vector<Note> notes;
 std::vector<MidiControllerEvent> controllers;
};
struct Clip { std::string name; std::string sourcePath; std::vector<float> stereo; std::vector<float> waveformPeaks; uint32_t sampleRate=48000; double startBeat=0; float gain=1; float fadeInSeconds=0,fadeOutSeconds=0; bool mute=false; bool equalPowerFades=false; };
enum class EqMode { LowShelf, Bell, HighShelf };
struct EqBand {
 EqMode mode=EqMode::Bell;
 float frequency=1000.f, gainDb=0.f, q=0.9f;
 bool enabled=true;
};
struct EqSettings {
 bool enabled=true;
 std::array<EqBand,8> bands{{
  {EqMode::LowShelf,95.f,0.f,0.707f},
  {EqMode::Bell,360.f,0.f,1.1f},
  {EqMode::Bell,2500.f,0.f,1.1f},
  {EqMode::HighShelf,9500.f,0.f,0.707f},
  {EqMode::Bell,650.f,0.f,1.1f},
  {EqMode::Bell,1300.f,0.f,1.1f},
  {EqMode::Bell,4800.f,0.f,1.1f},
  {EqMode::Bell,12500.f,0.f,1.1f}
 }};
};
struct CompressorSettings {
 bool enabled=false;
 float thresholdDb=-18.f, ratio=3.f, attackMs=10.f, releaseMs=120.f, makeupDb=0.f;
};
struct BiquadCoefficients { double b0=1,b1=0,b2=0,a1=0,a2=0; };
struct BiquadState { double z1=0,z2=0; float process(float x,const BiquadCoefficients& c); void reset(){z1=z2=0;} };
BiquadCoefficients makeEqCoefficients(const EqBand& band, uint32_t sampleRate);
double eqMagnitudeDb(const EqSettings& eq, double frequency, uint32_t sampleRate);
struct SynthSettings {
 float attackMs=8.f, releaseMs=170.f, sustain=0.85f;
 int sampleRootNote=60;
 // Original band-limited 4-table synth: sine -> saw -> square -> triangle.
 float wavetableMorph=.35f, wavetableDetuneCents=7.f, wavetableSecondOscMix=.23f;
};
// Original channel creative effects, intentionally allocation-free in the callback.
struct CreativeFxSettings {
 bool chorusEnabled=false;
 float chorusMix=.28f, chorusDepthMs=3.f, chorusRateHz=.35f;
 bool autopanEnabled=false;
 float autopanDepth=.5f, autopanPeriodBeats=2.f;
 bool crusherEnabled=false;
 int crusherBits=10, crusherHoldFrames=4;
 float crusherMix=.65f;
 // Three original v1.1 processors; deterministic per sample, safe for stream blocks.
 bool tremoloEnabled=false;
 float tremoloDepth=.65f, tremoloPeriodBeats=1.f;
 bool ringModEnabled=false;
 float ringModFrequencyHz=34.f, ringModMix=.55f;
 bool tapeEnabled=false;
 float tapeDrive=2.5f, tapeMix=.65f;
 // v1.25: new independent original processors, bypassed by default.
 bool resonatorEnabled=false;
 float resonatorHz=220.f, resonatorFeedback=.70f, resonatorMix=.35f;
 bool wahEnabled=false;
 float wahRateHz=.7f,wahDepth=.7f,wahMix=.55f;
 bool foldEnabled=false;
 float foldDrive=2.5f,foldMix=.5f;
 // v1.29 three additional independent, allocation-free processors.
 bool diodeEnabled=false; float diodeDrive=3.f, diodeMix=.5f;
 bool octaveFuzzEnabled=false; float octaveFuzzDrive=2.5f, octaveFuzzMix=.4f;
 bool crossfeedEnabled=false; float crossfeedAmount=.16f;
 bool pingPongEnabled=false;
 float pingPongDelayMs=280.f,pingPongFeedback=.42f,pingPongMix=.35f;
 // v1.12: three independent original real-time effects.
 bool phaserEnabled=false;
 float phaserRateHz=.35f, phaserDepth=.7f, phaserFeedback=.15f, phaserMix=.45f;
 bool tiltEnabled=false;
 float tiltDb=0.f; // -12..+12 dB spectral tilt around ~750 Hz
 bool exciterEnabled=false;
 float exciterDrive=2.5f, exciterMix=.3f;
};
// Veysona 1.3 original built-in effects: independent from third-party plugins.
struct RoomReverbSettings {
 bool enabled=false;
 float size=.58f, damping=.45f, mix=.25f;
};
struct DeEsserSettings {
 bool enabled=false;
 float frequencyHz=5600.f, thresholdDb=-30.f, reduction=.6f;
};
struct TransientSettings {
 bool enabled=false;
 float attack=.45f, sustain=0.f; // -1..+1; linked detector avoids stereo image shifts
};
struct MasterSettings {
 float gain=0.85f;
 bool limiterEnabled=true;
 float ceilingDb=-1.f;
};
struct SamplerZone {
 int rootNote=60, lowNote=0, highNote=127;
 float velocityLow=0.f, velocityHigh=1.f; // optional expressive sampled velocity layers
 uint32_t sampleRate=48000;
 std::vector<float> stereo;
 std::string name;
};
struct DrumSlot {
 int midiNote=36;
 uint32_t sampleRate=48000;
 std::vector<float> stereo;
 std::string name;
};
// Sample-accurate envelopes, stored in musical beats. Value ranges are
// validated on import rather than trusted from project JSON.
enum class AutomationTarget { TrackGain, TrackPan, FilterCutoff, Drive, DelayMix, EqLowGain, EqHighGain, BusGain };
struct AutomationPoint { double beat=0; float value=0; };
struct AutomationLane {
 AutomationTarget target=AutomationTarget::TrackGain;
 bool enabled=true, boolStep=false;
 std::vector<AutomationPoint> points;
 bool smooth=false; // cubic ease-in/ease-out; backward compatible linear default
};
float automate(const std::vector<AutomationLane>& lanes, AutomationTarget target, double beat, float fallback);
void setAutomationPoint(std::vector<AutomationLane>& lanes,AutomationTarget target,double beat,float value);
struct GateSettings { bool enabled=false; float thresholdDb=-43.f, attackMs=2.f, releaseMs=100.f; };
struct Bus {
 std::string name="Bus";
 bool muted=false;
 float gain=1.f, pan=0.f, width=1.f;
 std::vector<AutomationLane> automation;
};
// Four independent stereo send slots; -1 means route to master.
constexpr size_t kMaxBuses=8;
struct PluginSlotData { std::string path; std::string stateBase64; };
struct Track {
 std::string name="Track"; std::vector<Clip> clips; std::vector<MidiRegion> midi;
 Instrument instrument=Instrument::Saw; float gain=.8f, pan=0, cutoff=1, drive=0, delayMix=0; bool mute=false, solo=false;
 EqSettings eq; CompressorSettings compressor; GateSettings gate; CreativeFxSettings creativeFx; RoomReverbSettings room; DeEsserSettings deEsser; TransientSettings transient;
 float stereoWidth=1.f;
 int outputBus=-1;
 std::array<float,kMaxBuses> sends{};
 std::vector<AutomationLane> automation;
 SynthSettings synth;
 std::vector<float> instrumentSample; // interleaved stereo sample used for keyboard MIDI sampler
 uint32_t instrumentSampleRate=48000;
 std::string instrumentSampleName;
 std::vector<SamplerZone> sampleZones; // nearest key-zone multisampling; legacy single-sample projects still load
 std::array<DrumSlot,8> drumRack{};
 bool drumRackRecordedOnly=false; // legacy synth preview remains available for old projects
 std::string pluginPath; // project metadata; load only after the user confirms a plugin
 std::string pluginStateBase64; // legacy single-slot project compatibility
 std::vector<PluginSlotData> pluginChain; // ordered insert chain; plugins reopened by explicit user choice
};
struct Session {
 uint32_t sampleRate=48000; double bpm=120; int beatsPerBar=4; std::vector<Track> tracks;
 MasterSettings master; std::vector<Bus> buses;
 double loopStartBeat=0, loopEndBeat=0; // end 0 = automatic arrangement length
 double secondsPerBeat() const { return 60.0 / bpm; }
 double duration() const;
 std::vector<float> render(double seconds) const;
};
struct TrackFxState {
 float lpL=0, lpR=0; std::vector<float> delayL, delayR; size_t delayPos=0;
 std::vector<float> chorusL,chorusR;size_t chorusPosition=0;
 float heldL=0,heldR=0;int crushCountdown=0;
 // Fixed-size delay rings prepared off the audio thread for room reverb.
 std::array<std::vector<float>,4> roomL{},roomR{};
 std::array<size_t,4> roomPositions{};
 std::array<float,4> roomFilterL{},roomFilterR{};
 float deEssLowL=0,deEssLowR=0,deEssDetector=0;
 float transientFast=0,transientSlow=0;
 // Four cascaded first-order allpass sections for each stereo phaser channel.
 std::array<float,4> phaserInL{},phaserInR{},phaserOutL{},phaserOutR{};
 float phaserFeedbackL=0.f,phaserFeedbackR=0.f;
 float tiltLowL=0.f,tiltLowR=0.f;
 float exciterLowL=0.f,exciterLowR=0.f;
 std::vector<float> resonatorL,resonatorR,pingL,pingR;
 size_t resonatorPos=0,pingPos=0;
 float resonatorFilterL=0.f,resonatorFilterR=0.f;
 float wahLowL=0.f,wahLowR=0.f,wahBandL=0.f,wahBandR=0.f;
 std::array<BiquadState,8> eqL{}, eqR{};
 std::array<BiquadCoefficients,8> eqSmoothed{}; bool eqCoefficientsReady=false;
 double compressorEnvelope=0; double gateEnvelope=0;
 void prepare(size_t delaySamples,uint32_t sampleRate);
};
struct EngineState {
 std::vector<TrackFxState> tracks;
 void prepare(size_t numTracks, uint32_t rate);
 void reset();
};
// Streaming API: no memory allocations inside renderTrackBlock provided EngineState is prepared.
// Call on one thread at a time; caller coordinates edits and real-time callbacks.
// Render one MIDI region at local musical beat, used by the Session View launcher.
std::array<float,2> renderMidiRegionSample(const Session& session,const Track& track,
                                           const MidiRegion& region,double localBeat);
void renderTrackBlock(const Session& session, size_t trackIndex, int64_t samplePosition,
                      int numFrames, float* left, float* right, EngineState& state,
                      bool builtInSynth=true,bool applyEffects=true,
                      std::optional<bool> anySoloHint=std::nullopt);
void processTrackEffects(const Session&,size_t trackIndex,int numFrames,float* left,float* right,EngineState&,int64_t samplePosition=0,
                         std::optional<bool> anySoloHint=std::nullopt);
void applyBus(const Bus& bus, const Session& session, int64_t samplePosition,int frames,float* left,float* right);
// Mix a rendered track into its output bus and auxiliary sends, with no heap operations.
void routeTrack(const Track& track,int frames,const float* left,const float* right,
                float* masterL,float* masterR,float* const* busL,float* const* busR,size_t numBuses);
// Simple, offline-only granular overlap-add stretcher. Preserves pitch approximately;
// transient smearing is expected. For serious warping a better algorithm is needed.
void stretchClipGranular(Clip& clip,double speedRatio);
// Waveform and loudness diagnostics, not a certified LUFS/true-peak implementation.
struct AudioStats { float peak=0, rms=0, crestDb=0, correlation=0; };
AudioStats measureAudio(const float* left,const float* right,int frames);

void buildWaveformPeaks(Clip& clip,size_t framesPerPeak=256);
// Non-destructive to the imported source on disk; changes the project-owned audio data.
Clip splitAudioClip(Clip& first,double secondsIntoClip,double bpm);
void reverseAudioClip(Clip& clip);
void normaliseAudioClip(Clip& clip,float targetPeak=.89f);
// Assemble multiple recorded takes into one project-owned clip with equal-power
// crossfades. Each edit specifies [source take, beginning/end in seconds].
struct TakeSlice { size_t takeIndex=0; double beginSeconds=0,endSeconds=0; };
Clip compAudioTakes(const std::vector<Clip>& takes,const std::vector<TakeSlice>& slices,
                    double crossfadeSeconds=.01);
// Hann-window real FFT magnitude in dBFS, for offline spectrum display/analysis;
// not a certified LUFS, true-peak or real-time analyzer.
std::vector<float> analyseSpectrum(const std::vector<float>& stereo,size_t startFrame=0,
                                    size_t fftSize=1024);
// Stereo-linked peak protection; fully deterministic, no allocations.
void processMasterBlock(const MasterSettings& settings,int numFrames,float* left,float* right);
// Load a source audio clip as a keyboard-playable instrument, preparing it off the audio thread.
void assignSamplerSample(Track& track,const Clip& source,int rootMidiNote=60);
void assignSamplerZones(Track& track,std::vector<SamplerZone> zones,const std::string& programName);
void assignDrumSample(Track& track,size_t slot,int midiNote,const Clip& source);
Clip readWav(const std::string& filename);
void writeWav(const std::string& filename, const std::vector<float>& stereo, uint32_t sampleRate);
void writeWavAdvanced(const std::string& filename,const std::vector<float>& stereo,
                      uint32_t sampleRate,WavFormat format,DitherMode dither=DitherMode::TPDF);
std::vector<float> makeDemoSound(int soundId, uint32_t sampleRate);
}
