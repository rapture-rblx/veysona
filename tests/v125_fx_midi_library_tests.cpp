#include "AudioCore.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
using namespace veysona;
namespace fs=std::filesystem;
static void require(bool pass,const char* reason){if(!pass)throw std::runtime_error(reason);}
static double energy(const std::vector<float>& a){double n=0;for(float x:a){require(std::isfinite(x),"non-finite sample");n+=double(x)*x;}return n;}
static double meanDiff(const std::vector<float>& a,const std::vector<float>& b){
 require(a.size()==b.size(),"size mismatch");double d=0;for(size_t i=0;i<a.size();++i)d+=std::abs(double(a[i])-b[i]);return d/a.size();
}
static std::vector<float> render(CreativeFxSettings settings){
 Session session;session.sampleRate=48000;session.bpm=120;session.master.gain=1.f;session.master.limiterEnabled=false;
 Track track;track.gain=1.f;track.eq.enabled=false;track.creativeFx=settings;
 Clip clip;clip.sampleRate=48000;clip.stereo.resize(48000*2);
 for(int i=0;i<48000;i++){float x=float(.32*std::sin(i*440.*2*kPi/48000.)+.13*std::sin(i*1200.*2*kPi/48000.));clip.stereo[size_t(2*i)]=x;clip.stereo[size_t(2*i+1)]=x*.83f;}
 track.clips.push_back(std::move(clip));session.tracks.push_back(std::move(track));return session.render(1.);
}
int main(){
 // MIDI channel 10 must be audible without a user-loaded soundfont, even in
 // mixed type-0 files or when the MIDI is played through a default instrument.
 Session midi;midi.sampleRate=48000;midi.master.gain=1.f;midi.master.limiterEnabled=false;
 Track drums;drums.instrument=Instrument::Saw;
 MidiRegion notes;notes.startBeat=0;notes.lengthBeats=4;
 for(int pitch:{36,38,42,49,51,62})notes.notes.push_back({pitch,0,.2f,.9f,10});
 drums.midi.push_back(notes);midi.tracks.push_back(std::move(drums));
 const auto audible=midi.render(.2);require(energy(audible)>1e-4,"MIDI GM percussion channel silent");
 // A non-drum MIDI channel is still played by its normal pitched instrument.
 midi.tracks[0].midi[0].notes[0].channel=1;
 const auto mixed=midi.render(.2);
 require(meanDiff(audible,mixed)>1e-5,"GM and melodic MIDI incorrectly share voice path");
 const auto dry=render({});
 CreativeFxSettings res;res.resonatorEnabled=true;res.resonatorHz=320.f;res.resonatorFeedback=.84f;res.resonatorMix=.65f;
 CreativeFxSettings wah;wah.wahEnabled=true;wah.wahRateHz=1.3f;wah.wahDepth=.92f;wah.wahMix=.75f;
 CreativeFxSettings fold;fold.foldEnabled=true;fold.foldDrive=5.4f;fold.foldMix=.8f;
 CreativeFxSettings ping;ping.pingPongEnabled=true;ping.pingPongDelayMs=125.f;ping.pingPongFeedback=.72f;ping.pingPongMix=.7f;
 for(const auto& setting:{res,wah,fold,ping}){
  const auto wet=render(setting);
  require(meanDiff(dry,wet)>.002,"new effect has no audible impact");
  require(energy(wet)<1e7,"new effect unstable");
 }
 CreativeFxSettings diode;diode.diodeEnabled=true;diode.diodeDrive=5.f;diode.diodeMix=.75f;
 CreativeFxSettings octave;octave.octaveFuzzEnabled=true;octave.octaveFuzzDrive=5.f;octave.octaveFuzzMix=.8f;
 CreativeFxSettings cross;cross.crossfeedEnabled=true;cross.crossfeedAmount=.35f;
 for(const auto& setting:{diode,octave,cross}){
  const auto wet=render(setting);
  require(meanDiff(dry,wet)>.002,"v1.29 processor has no audible effect");
  require(energy(wet)<1e7,"v1.29 processor unstable");
 }
 size_t newInstruments=0;
 for(const auto& file:fs::directory_iterator(fs::path(VEYSONA_SOURCE_PATH)/"assets/instruments"))
  if(file.path().extension()==".wav"&&file.path().filename().string().starts_with("V129 ")){
   ++newInstruments;
   const auto clip=readWav(file.path().string());
   require(clip.sampleRate==48000&&!clip.stereo.empty(),"v1.29 instrument invalid");
  }
 require(newInstruments==96,"v1.29 instrument bank incomplete");
 CreativeFxSettings all;all.resonatorEnabled=all.wahEnabled=all.foldEnabled=all.pingPongEnabled=true;
 all.resonatorFeedback=100;all.wahRateHz=1000;all.foldDrive=100;all.pingPongFeedback=100;
 require(energy(render(all))<1e7,"extreme effect parameters unstable");
 const auto root=fs::path(VEYSONA_SOURCE_PATH);
 size_t existing=0,unique=0,presets=0;
 std::unordered_set<std::string> filenames;
 for(const auto& f:fs::directory_iterator(root/"assets/samples"))if(f.path().extension()==".wav"){
  ++existing;if(f.path().filename().string().starts_with("V125 ")){
   ++unique;filenames.insert(f.path().filename().string());
   std::ifstream wav(f.path(),std::ios::binary);char header[12]{};wav.read(header,12);
   require(wav.gcount()==12&&std::string(header,4)=="RIFF"&&std::string(header+8,4)=="WAVE","new sound is not WAV");
  }
 }
 for(const auto& f:fs::directory_iterator(root/"assets/effect_presets"))if(f.path().filename().string().starts_with("V125 ")&&f.path().extension()==".json")++presets;
 require(existing>=2033 && unique==1280 && filenames.size()==1280,"sample count or filenames mismatch");
 require(presets==80,"new effect preset count mismatch");
 const auto brand=root/"assets/branding/Veysona-Logo.png";
 require(fs::exists(brand),"transparent brand missing");
 std::ifstream logo(brand,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(logo)),{});
 require(bytes.size()>32 && bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)==0 && (unsigned char)bytes[25]==6,"Veysona logo is not alpha PNG RGBA");
 std::cout<<"v1.25 MIDI GM ch10, four native effects, "<<existing<<" WAVs, "<<presets<<" new presets and transparent logo verified\n";
}
