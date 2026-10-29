#include "AudioCore.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace veysona;
int main(int argc,char** argv){
 if(argc!=4){std::cerr<<"Usage: veysona_v11_demo <samples directory> <instruments directory> <output wav>\n";return 2;}
 try{
  Session session;session.sampleRate=48000;session.bpm=120;session.master.gain=.78f;session.master.ceilingDb=-1.f;
  Track drums;drums.name="House groove";drums.gain=.53f;drums.pan=-.05f;drums.eq.enabled=false;
  for(int i=0;i<4;++i){auto clip=readWav(std::string(argv[1])+"/Loop - Pulse House 120BPM.wav");clip.startBeat=i*8;clip.name="2-bar loop";drums.clips.push_back(std::move(clip));}
  session.tracks.push_back(std::move(drums));
  Track bass;bass.name="FM bass";bass.gain=.70f;auto bassWav=readWav(std::string(argv[2])+"/FM Sub Bass C3.wav");assignSamplerSample(bass,bassWav,48);
  MidiRegion bassMidi;bassMidi.name="Bass movement";bassMidi.lengthBeats=32;
  const int bassLine[]={48,48,55,51,48,51,55,46};
  for(int bar=0;bar<8;++bar){bassMidi.notes.push_back({bassLine[bar],bar*4.,2.1f,.78f});bassMidi.notes.push_back({bassLine[bar],bar*4.+2.5,.75f,.52f});}
  bass.midi.push_back(bassMidi);bass.creativeFx.tapeEnabled=true;bass.creativeFx.tapeDrive=2.7f;bass.creativeFx.tapeMix=.4f;
  session.tracks.push_back(std::move(bass));
  Track keys;keys.name="Felt keyboard";keys.gain=.65f;keys.pan=.25f;auto keysWav=readWav(std::string(argv[2])+"/Lo-Fi Rhodes C4.wav");assignSamplerSample(keys,keysWav,60);
  MidiRegion chords;chords.name="Original chords";chords.lengthBeats=32;
  for(int b=0;b<8;++b){int root=(b%4==0?60:b%4==1?56:b%4==2?63:58);for(int st:{0,3,7})chords.notes.push_back({root+st,b*4.,3.75f,.39f});}
  keys.midi.push_back(chords);keys.creativeFx.tremoloEnabled=true;keys.creativeFx.tremoloDepth=.28f;keys.creativeFx.tremoloPeriodBeats=2;
  session.tracks.push_back(std::move(keys));
  Track synth;synth.name="Glass Pluck";synth.gain=.32f;synth.pan=-.25f;auto pluck=readWav(std::string(argv[2])+"/Pluck Harp C4.wav");assignSamplerSample(synth,pluck,60);
  MidiRegion melody;melody.lengthBeats=32;melody.name="High notes";
  const int notes[]={72,75,79,82,79,75,72,70,68,72,75,77,79,75,72,67};
  for(int i=0;i<16;++i)melody.notes.push_back({notes[i],i*2.,.55f,.45f});
  synth.midi.push_back(melody);synth.creativeFx.ringModEnabled=true;synth.creativeFx.ringModFrequencyHz=6.f;synth.creativeFx.ringModMix=.16f;
  session.tracks.push_back(std::move(synth));
  auto output=session.render(16.);writeWavAdvanced(argv[3],output,48000,WavFormat::PCM24);
  std::cout<<"Veysona 1.1: 16-second, four-track, 48 kHz / 24-bit demo exported: "<<argv[3]<<"\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
