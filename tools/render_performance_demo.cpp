#include "AudioCore.h"
#include "Performance.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
using namespace veysona;
static void addSample(std::vector<float>& dest,const Clip& sample,int64_t position,float gain){
 const size_t frames=sample.stereo.size()/2;
 for(size_t i=0;i<frames&&position+int64_t(i)<int64_t(dest.size()/2);++i){
  auto p=size_t(position)+i;dest[p*2]+=sample.stereo[i*2]*gain;dest[p*2+1]+=sample.stereo[i*2+1]*gain;
 }
}
static Clip makeDrumLoop(const std::filesystem::path& samples,bool variation,uint32_t rate,double bpm){
 auto kick=readWav((samples/"Kick - Punch.wav").string());
 auto snare=readWav((samples/"Snare - Clean.wav").string());
 auto hat=readWav((samples/"HiHat - Closed.wav").string());
 Clip clip;clip.name=variation?"Second drum scene":"First drum scene";clip.sampleRate=rate;
 const size_t frames=size_t(std::llround(4*60./bpm*rate));clip.stereo.assign(frames*2,0.f);
 const auto beatToFrame=[&](double beat){return int64_t(std::llround(beat*60./bpm*rate));};
 for(int beat=0;beat<4;++beat){
  if(beat==0||beat==2||variation&&beat==3)addSample(clip.stereo,kick,beatToFrame(beat),.66f);
  if(beat==1||beat==3)addSample(clip.stereo,snare,beatToFrame(beat),.42f);
  for(int sub=0;sub<2;++sub)addSample(clip.stereo,hat,beatToFrame(beat+sub*.5),.17f);
 }
 buildWaveformPeaks(clip);return clip;
}
static MidiRegion notes(std::string name,int root,bool variation=false){
 MidiRegion r;r.name=std::move(name);r.lengthBeats=4;
 for(int i=0;i<8;++i){int pitch=root+(variation?std::array<int,8>{0,7,10,7,0,3,7,10}[size_t(i)]:std::array<int,8>{0,0,7,0,10,7,3,0}[size_t(i)]);
  r.notes.push_back({pitch,i*.5,.4f,.7f,1});}
 return r;
}
int main(int argc,char**argv){
 if(argc<4){std::cerr<<"Usage: veysona_performance_demo path/to/samples path/to/instruments output.wav\n";return 1;}
 const auto samples=std::filesystem::path(argv[1]),instruments=std::filesystem::path(argv[2]);
 Session session;session.sampleRate=48000;session.bpm=124;session.beatsPerBar=4;
 session.master.gain=.73f;session.master.limiterEnabled=true;
 Track drums;drums.name="DRUM RACK / LOOPS";drums.clips.push_back(makeDrumLoop(samples,false,session.sampleRate,session.bpm));
 drums.clips.push_back(makeDrumLoop(samples,true,session.sampleRate,session.bpm));drums.gain=.92f;
 session.tracks.push_back(drums);
 Track bass;bass.name="FM BASS";bass.instrument=Instrument::Sampler;assignSamplerSample(bass,readWav((instruments/"Analog Bass C3.wav").string()),48);
 bass.midi.push_back(notes("Root bass",36));bass.midi.push_back(notes("Bass variation",41,true));bass.gain=.7f;
 session.tracks.push_back(bass);
 Track keys;keys.name="WARM KEYS";keys.instrument=Instrument::Sampler;assignSamplerSample(keys,readWav((instruments/"Warm Electric Piano.wav").string()),60);
 MidiRegion chords;chords.name="Four chord cycle";chords.lengthBeats=4;
 for(int p:{60,64,67})chords.notes.push_back({p,0,3.5,.55f,1});
 keys.midi.push_back(chords);chords.name="Suspended chord";chords.notes.clear();
 for(int p:{62,67,69})chords.notes.push_back({p,0,3.5,.55f,1});keys.midi.push_back(chords);
 keys.gain=.62f;keys.creativeFx.chorusEnabled=true;keys.creativeFx.chorusMix=.34f;
 session.tracks.push_back(keys);
 Track lead;lead.name="ANALOG PLUCK";lead.instrument=Instrument::Triangle;
 lead.midi.push_back(notes("Lead theme",72));lead.midi.push_back(notes("Lead answer",76,true));lead.gain=.58f;
 lead.creativeFx.autopanEnabled=true;lead.creativeFx.autopanDepth=.36f;lead.creativeFx.autopanPeriodBeats=4;
 session.tracks.push_back(lead);
 PerformanceLauncher launcher;launcher.prepare(session.tracks.size());launcher.setQuantisation(4);
 EngineState state;state.prepare(session.tracks.size(),session.sampleRate);
 launcher.queueScene(session,0,0);
 const int block=256;const size_t total=size_t(24)*session.sampleRate;
 std::vector<float> stereo(total*2);std::array<float,block> l{},r{},ml{},mr{};
 bool secondScene=false,backFirst=false;
 for(size_t pos=0;pos<total;pos+=block){
  const auto nowBeat=double(pos)/session.sampleRate*session.bpm/60.;
  if(!secondScene&&nowBeat>=16){launcher.queueScene(session,1,nowBeat);secondScene=true;}
  if(!backFirst&&nowBeat>=32){launcher.queueScene(session,0,nowBeat);backFirst=true;}
  const int n=int(std::min(size_t(block),total-pos));std::fill_n(ml.data(),n,0.f);std::fill_n(mr.data(),n,0.f);
  for(size_t ti=0;ti<session.tracks.size();++ti){
   launcher.renderTrack(session,ti,int64_t(pos),n,l.data(),r.data());
   processTrackEffects(session,ti,n,l.data(),r.data(),state,int64_t(pos));
   for(int i=0;i<n;++i){ml[i]+=l[i];mr[i]+=r[i];}
  }
  processMasterBlock(session.master,n,ml.data(),mr.data());
  for(int i=0;i<n;++i){stereo[(pos+size_t(i))*2]=ml[i];stereo[(pos+size_t(i))*2+1]=mr[i];}
 }
 writeWavAdvanced(argv[3],stereo,session.sampleRate,WavFormat::PCM24);
 std::cout<<"Rendered "<<total<<" frames / 24-bit WAV / 124 BPM / quantised clips and scene switching\n";
}
