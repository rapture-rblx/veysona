#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using namespace veysona;
#define CHECK(e) do { if(!(e))throw std::runtime_error(std::string("v112 check failed at line ")+std::to_string(__LINE__)+": " #e); } while(false)
static double rms(const std::vector<float>& a) {
 double sum=0;for(float f:a){CHECK(std::isfinite(f));sum+=double(f)*f;}
 return std::sqrt(sum/std::max(size_t(1),a.size()));
}
static double delta(const std::vector<float>& a,const std::vector<float>& b){
 CHECK(a.size()==b.size());double sum=0;
 for(size_t i=0;i<a.size();++i)sum+=std::abs(double(a[i])-b[i]);
 return sum/a.size();
}
static std::vector<float> renderFx(const CreativeFxSettings& effect,int frameSize){
 Session s;s.sampleRate=48000;s.bpm=120;s.master.gain=1;s.master.limiterEnabled=false;
 Track t;t.gain=1;t.pan=0;t.eq.enabled=false;t.creativeFx=effect;
 Clip audio;audio.name="test signal";audio.sampleRate=48000;
 for(int i=0;i<48000;++i){
  float x=.18f*std::sin(float(2*kPi*440.*i/48000.))+.12f*std::sin(float(2*kPi*2300.*i/48000.));
  audio.stereo.push_back(x);audio.stereo.push_back(x*.8f);
 }
 t.clips.push_back(std::move(audio));s.tracks.push_back(std::move(t));
 if(frameSize==0)return s.render(1.);
 EngineState state;state.prepare(s.tracks.size(),s.sampleRate);
 std::vector<float> out(48000*2);std::vector<float> l(static_cast<size_t>(frameSize),0.f),r(static_cast<size_t>(frameSize),0.f);
 // Note: renderTrackBlock already applies the track FX in this engine.
 for(int pos=0;pos<48000;pos+=frameSize){
  int frames=std::min(frameSize,48000-pos);
  renderTrackBlock(s,0,pos,frames,l.data(),r.data(),state);
  for(int i=0;i<frames;++i){out[2*(pos+i)]=l[i];out[2*(pos+i)+1]=r[i];}
 }
 return out;
}
int main(){
 const fs::path assets=fs::path(VEYSONA_SOURCE_PATH)/"assets";
 auto count=[&](const std::string& dirname,const std::string& ext){size_t all=0, newFiles=0;for(auto& f:fs::directory_iterator(assets/dirname)){
  if(f.path().string().ends_with(ext)){++all;if(f.path().filename().string().starts_with("V12 "))++newFiles;}}
  return std::pair{all,newFiles};};
 const auto samples=count("samples",".wav"),instruments=count("instruments",".wav");
 const auto midi=count("midi",".mid"),presets=count("effect_presets",".vfx.json");
 CHECK(samples.first>=753 && samples.second==240);
 CHECK(instruments.first==456 && instruments.second==120);
 CHECK(midi.first==126 && midi.second==48);
 CHECK(presets.first>=132 && presets.second==60);
 CHECK(fs::exists(assets/"V112_SOUND_BANK.json"));
 size_t waves=0;double energy=0;
 for(auto folder:{"samples","instruments"})for(auto& f:fs::directory_iterator(assets/folder)){
  if(!f.path().filename().string().starts_with("V12 ")||f.path().extension()!=".wav")continue;
  const auto clip=readWav(f.path().string());CHECK(clip.sampleRate==48000 && !clip.stereo.empty());
  for(auto v:clip.stereo){CHECK(std::isfinite(v)&&std::abs(v)<=1.0001f);energy+=v*v;}
  ++waves;
 }CHECK(waves==360 && energy>1000.);
 for(auto& f:fs::directory_iterator(assets/"midi")){
  if(!f.path().filename().string().starts_with("V12 "))continue;
  std::ifstream in(f.path(),std::ios::binary);char hdr[4]{};in.read(hdr,4);CHECK(std::string(hdr,4)=="MThd");
 }
 for(auto& f:fs::directory_iterator(assets/"effect_presets")){
  if(!f.path().filename().string().starts_with("V12 "))continue;
  std::ifstream in(f.path());std::string j((std::istreambuf_iterator<char>(in)),{});
  for(auto key:{"veysona-fx-v1","phaserEnabled","tiltEnabled","exciterEnabled","eq","compressor"})CHECK(j.find(key)!=std::string::npos);
 }
 // Confirm a newly generated C2 instrument is playable at MIDI note 36.
 const auto bassSample=readWav((assets/"instruments"/"V12 Bass - Rounded Sub C2.wav").string());
 Track sampleTrack;assignSamplerSample(sampleTrack,bassSample,36);
 MidiRegion bassNotes;bassNotes.lengthBeats=4;bassNotes.notes.push_back({36,0,1.,.8f});
 sampleTrack.midi.push_back(std::move(bassNotes));
 Session bassSession;bassSession.tracks.push_back(std::move(sampleTrack));
 CHECK(rms(bassSession.render(.4))>.0005);
 const auto original=renderFx({},0);CHECK(rms(original)>.03);
 CreativeFxSettings phaser;phaser.phaserEnabled=true;phaser.phaserRateHz=.7f;phaser.phaserMix=.67f;
 CreativeFxSettings tilt;tilt.tiltEnabled=true;tilt.tiltDb=8.f;
 CreativeFxSettings exciter;exciter.exciterEnabled=true;exciter.exciterDrive=7.f;exciter.exciterMix=.8f;
 const auto phaserOut=renderFx(phaser,0),tiltOut=renderFx(tilt,0),exciterOut=renderFx(exciter,0);
 CHECK(delta(original,phaserOut)>.01);
 CHECK(delta(original,tiltOut)>.01);
 CHECK(delta(original,exciterOut)>.001);
 // Explicit block processing must give equivalent results to full offline rendering.
 CHECK(delta(phaserOut,renderFx(phaser,257))<.003);
 CHECK(delta(tiltOut,renderFx(tilt,257))<.003);
 CHECK(delta(exciterOut,renderFx(exciter,257))<.003);
 CreativeFxSettings everything;everything.phaserEnabled=everything.tiltEnabled=everything.exciterEnabled=true;
 everything.phaserFeedback=5.f;everything.phaserRateHz=1200.f;everything.tiltDb=100.f;
 everything.exciterDrive=400.f;everything.exciterMix=5.f;
 const auto extreme=renderFx(everything,0);CHECK(rms(extreme)<2.0);
 std::cout<<"Veysona v1.12: "<<samples.first<<" samples, "<<instruments.first<<" sampled instruments, "
          <<midi.first<<" MIDI patterns, "<<presets.first<<" effect-chain presets and 3 DSP effects passed.\n";
}
