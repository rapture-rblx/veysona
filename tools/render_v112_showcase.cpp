#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace veysona;
int main(int argc,char** argv){
 try{
  if(argc<3)throw std::runtime_error("Usage: veysona_v112_showcase <asset-directory> <output.wav>");
  const auto root=std::filesystem::path(argv[1]);
  Session session;session.sampleRate=48000;session.bpm=124;session.master.gain=.56f;
  // Import an actual v1.12 loop, and repeat it as clips for a 16-second mix.
  Track drums;drums.name="V12 House Percussion";drums.gain=.60f;
  const auto loop=readWav((root/"samples"/"V12 Loop - House 124 BPM 01.wav").string());
  drums.clips.push_back(loop);drums.clips.back().name="House loop";
  drums.clips.push_back(loop);drums.clips.back().startBeat=16;
  drums.creativeFx.tiltEnabled=true;drums.creativeFx.tiltDb=2.f;
  session.tracks.push_back(std::move(drums));
  auto instrumentTrack=[&](const std::string& name,const char* asset,int rootNote,float level){
   Track track;track.name=name;track.gain=level;
   const auto sample=readWav((root/"instruments"/asset).string());
   assignSamplerSample(track,sample,rootNote);
   MidiRegion region;region.name=name+" MIDI";region.lengthBeats=32;
   track.midi.push_back(std::move(region));
   return track;
  };
  Track bass=instrumentTrack("Bass","V12 Bass - Rounded Sub C2.wav",36,.62f);
  for(int b=0;b<32;b+=2)bass.midi[0].notes.push_back({36+(b%8==6?7:0),double(b),1.6f,.76f});
  bass.creativeFx.exciterEnabled=true;bass.creativeFx.exciterDrive=2.0f;bass.creativeFx.exciterMix=.14f;
  session.tracks.push_back(std::move(bass));
  Track keys=instrumentTrack("Velvet Keys","V12 Keys - Velvet C4.wav",60,.55f);
  for(int b=0;b<32;b+=4){for(int p:{60,64,67})keys.midi[0].notes.push_back({p,double(b),3.15f,.64f});}
  keys.creativeFx.phaserEnabled=true;keys.creativeFx.phaserRateHz=.4f;
  keys.creativeFx.phaserDepth=.7f;keys.creativeFx.phaserMix=.52f;
  session.tracks.push_back(std::move(keys));
  Track lead=instrumentTrack("Lead","V12 Lead - Cutting C4.wav",60,.26f);
  for(int b=0;b<32;b+=2)lead.midi[0].notes.push_back({72+((b/2)%4)*2,double(b),.8f,.52f});
  lead.creativeFx.exciterEnabled=true;lead.creativeFx.exciterDrive=4.f;lead.creativeFx.exciterMix=.40f;
  session.tracks.push_back(std::move(lead));
  const auto mix=session.render(16.);
  float peak=0;for(auto x:mix){if(!std::isfinite(x))throw std::runtime_error("Nonfinite output");peak=std::max(peak,std::abs(x));}
  writeWavAdvanced(argv[2],mix,session.sampleRate,WavFormat::PCM24);
  std::cout<<"Rendered "<<mix.size()/2<<" frames of v1.12 audio, sample peak "<<peak<<"\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"V1.12 demo failed: "<<e.what()<<"\n";return 1;}
}
