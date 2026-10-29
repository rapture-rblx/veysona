#include "AudioCore.h"
#include "MidiTools.h"
#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
using namespace veysona;
int main(int argc,char** argv){
 if(argc!=4){std::cerr<<"Usage: veysona_v13_demo <samples> <instruments> <output.wav>\n";return 2;}
 try{
  Session s;s.bpm=122;s.sampleRate=48000;s.master.gain=.72f;s.master.ceilingDb=-1.f;
  Track drums;drums.name="House percussion";drums.eq.enabled=false;drums.gain=.43f;
  auto sample=readWav(std::string(argv[1])+"/V13 Loop - Deep House 122 BPM.wav");
  for(int b=0;b<32;b+=8){auto clip=sample;clip.startBeat=b;drums.clips.push_back(std::move(clip));}
  drums.transient={true,.85f,-.18f};
  s.tracks.push_back(std::move(drums));
  Track bass;bass.name="Analog Bass";bass.gain=.48f;
  assignSamplerSample(bass,readWav(std::string(argv[2])+"/Analog Square Bass C3.wav"),48);
  MidiRegion bassPattern;bassPattern.lengthBeats=32;
  for(int b=0;b<32;b+=4)for(int j=0;j<4;++j)bassPattern.notes.push_back({int(48+(b/4%4==1?3:b/4%4==2?7:0)),double(b+j),.71,.65f});
  bass.midi.push_back(std::move(bassPattern));bass.creativeFx.tapeEnabled=true;bass.creativeFx.tapeDrive=2.f;bass.creativeFx.tapeMix=.4f;
  s.tracks.push_back(std::move(bass));
  Track pad;pad.name="Ambient pad";pad.gain=.44f;pad.pan=.22f;
  assignSamplerSample(pad,readWav(std::string(argv[2])+"/V13 Northern Pad C4.wav"),60);
  MidiRegion chords;chords.lengthBeats=32;for(int bar=0;bar<8;++bar){auto root=(bar%4==0?60:bar%4==1?56:bar%4==2?63:58);for(int semi:{0,3,7})chords.notes.push_back({root+semi,bar*4.,3.6,.41f});}
  pad.midi.push_back(std::move(chords));pad.room={true,.78f,.42f,.24f};
  s.tracks.push_back(std::move(pad));
  Track lead;lead.name="Glass melody";lead.gain=.35f;lead.pan=-.18f;
  assignSamplerSample(lead,readWav(std::string(argv[2])+"/V13 Celeste Bell C4.wav"),60);
  MidiRegion melody;melody.lengthBeats=32;
  melody.notes=midi::makeEuclideanRhythm(32,15,72,1.,2,.67f);
  const int scale[]={0,3,7,10,12,7,3,5};for(size_t i=0;i<melody.notes.size();++i)melody.notes[i].pitch=72+scale[i%8];
  lead.midi.push_back(std::move(melody));lead.room={true,.36f,.55f,.18f};
  s.tracks.push_back(std::move(lead));
  auto audio=s.render(16.);writeWavAdvanced(argv[3],audio,s.sampleRate,WavFormat::PCM24);
  std::cout<<"Rendered Veysona 1.3 original 16s/122BPM/24-bit showcase to "<<argv[3]<<"\n";
 }catch(const std::exception& ex){std::cerr<<"Showcase error: "<<ex.what()<<"\n";return 1;}
}
