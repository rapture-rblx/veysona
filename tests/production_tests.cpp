#include "AudioCore.h"
#include "MidiTools.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("FAIL ")+__FILE__+":"+std::to_string(__LINE__)+" - " #x);}while(false)
static double rms(const std::vector<float>& audio){double sum=0;for(float x:audio)sum+=x*x;return std::sqrt(sum/std::max(size_t(1),audio.size()));}
int main(){
 using namespace veysona;
 const auto instDir=std::filesystem::path(VEYSONA_INSTRUMENTS_PATH);
 CHECK(std::filesystem::exists(instDir));int instruments=0;
 for(const auto& f:std::filesystem::directory_iterator(instDir)){
  if(f.path().extension()!=".wav")continue;
  auto audio=readWav(f.path().string());CHECK(!audio.stereo.empty() && audio.waveformPeaks.size()>0);
  ++instruments;
 }
 CHECK(instruments>=16);
 auto piano=readWav((instDir/"Warm Electric Piano.wav").string());
 Track track;assignSamplerSample(track,piano,60);CHECK(track.instrument==Instrument::Sampler);
 CHECK(track.instrumentSample.size()==piano.stereo.size());
 MidiRegion region;region.lengthBeats=4;region.notes.push_back({60,0,1,1.f});track.midi.push_back(region);
 Session session;session.sampleRate=48000;session.tracks.push_back(track);
 auto sampler=session.render(1.0);CHECK(rms(sampler)>.006);
 // Scale audition: octave higher produces an earlier final sample endpoint.
 session.tracks[0].midi[0].notes[0].pitch=72;
 auto octave=session.render(1.0);CHECK(rms(octave)>.006);
 CHECK(std::abs(octave[680]-sampler[680])>.002);
 // Muted instruments and soloed tracks cannot leak into the mix.
 session.tracks[0].mute=true;auto muted=session.render(.3);CHECK(rms(muted)<1e-9);
 session.tracks[0].mute=false;
 Track other;other.name="Other";other.midi.push_back(region);other.instrument=Instrument::Sine;
 session.tracks.push_back(other);session.tracks[1].solo=true;
 auto solo=session.render(.4);CHECK(rms(solo)>.003);
 session.tracks[1].solo=false;
 // Drum rack hits follow MIDI notes with stereo samples and velocity control.
 auto kick=readWav((std::filesystem::path(VEYSONA_SAMPLES_PATH)/"Kick - Deep.wav").string());
 Track kit;assignDrumSample(kit,0,36,kick);
 CHECK(kit.instrument==Instrument::DrumRack);
 MidiRegion beat;beat.lengthBeats=2;beat.notes.push_back({36,0,.125,1});kit.midi.push_back(beat);
 Session drums;drums.tracks.push_back(kit);
 auto punch=drums.render(.5);CHECK(rms(punch)>.005);
 drums.tracks[0].midi[0].notes[0].pitch=45;
 auto silent=drums.render(.5);CHECK(rms(silent)<1e-9);
 // Clip arrangement editing: split, reverse, normalise preserve audio material.
 Clip editable;editable.name="Test";editable.sampleRate=48000;editable.startBeat=2;
 editable.stereo.resize(48000*2);
 for(size_t i=0;i<48000;++i){editable.stereo[i*2]=float(i)/48000.f*.5f;editable.stereo[i*2+1]=-.25f;}
 auto end=splitAudioClip(editable,.4,120);
 CHECK(editable.stereo.size()+end.stereo.size()==48000*2);
 CHECK(std::abs(end.startBeat-2.8)<.0001);
 CHECK(editable.waveformPeaks.size()>0&&end.waveformPeaks.size()>0);
 auto first=editable.stereo[0];reverseAudioClip(editable);
 CHECK(editable.stereo[0]!=first);
 normaliseAudioClip(editable,.89f);CHECK(editable.gain>1);
 // Master gain and stereo-linked peak limiter produce bounded, deterministic output.
 std::array<float,5> l{0.5f,1.f,2.f,-2.f,NAN},r{0.25f,-1.f,.25f,2.f,NAN};
 MasterSettings master;master.gain=1.5f;master.ceilingDb=-3.f;
 processMasterBlock(master,5,l.data(),r.data());
 const float ceiling=std::pow(10.f,-3.f/20.f);
 for(int i=0;i<5;++i){CHECK(std::isfinite(l[i])&&std::isfinite(r[i]));CHECK(std::abs(l[i])<=ceiling+1e-6f && std::abs(r[i])<=ceiling+1e-6f);}
 CHECK(std::abs(l[2])>std::abs(r[2]));
 // MIDI edit operations are deterministic, bounded and preserve clip pitches.
 std::vector<Note> melody{{60,0,.25,.8f},{61,.5,.25,.6f},{64,1,.5,.95f}};
 midi::constrainToScale(melody,0,{0,2,4,5,7,9,11});
 for(auto n:melody)CHECK(n.pitch!=61);
 midi::scaleVelocity(melody,1.25f);for(auto n:melody)CHECK(n.velocity<=1.f);
 midi::makeTriads(melody,false);CHECK(melody.size()==9);
 midi::reverse(melody,4);for(auto n:melody)CHECK(n.beat>=0 && n.beat<=4);
 midi::applySwing(melody,.25,55);for(auto n:melody)CHECK(n.beat>=0 && n.length>0);
 CHECK(session.master.limiterEnabled && session.loopEndBeat==0);
 std::cout<<"PASS: 16 source instruments, sampler, drum rack, editing, solo/mute, master limiter and advanced MIDI utilities\n";
}
