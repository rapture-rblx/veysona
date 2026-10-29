#include "AudioCore.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace veysona;
int main(int argc,char**argv){
 if(argc!=4){std::cerr<<"Usage: veysona_v08_demo <samples-folder> <instruments-folder> <out.wav>\n";return 2;}
 std::filesystem::path samples=argv[1],inst=argv[2];
 Session s;s.bpm=120;s.sampleRate=48000;s.master.gain=.8;s.master.limiterEnabled=true;
 Track drums;drums.name="Drum Rack";drums.gain=.83;
 auto drum=[&](int slot,int note,const char* name){auto c=readWav((samples/name).string());assignDrumSample(drums,size_t(slot),note,c);};
 drum(0,36,"Kick - Deep.wav");drum(1,38,"Snare - Clean.wav");drum(2,42,"HiHat - Closed.wav");drum(3,46,"HiHat - Open.wav");
 MidiRegion kit;kit.lengthBeats=16;
 for(int bar=0;bar<4;++bar)for(int eighth=0;eighth<8;++eighth){
  double b=bar*4+eighth*.5;
  kit.notes.push_back({42,b,.125,eighth%2?.35f:.65f});
  if(eighth==0||eighth==4)kit.notes.push_back({36,b,.125,.9f});
  if(eighth==2||eighth==6)kit.notes.push_back({38,b,.125,.72f});
 }
 drums.midi.push_back(kit);s.tracks.push_back(drums);
 Track bass;bass.name="Sub Bass";bass.instrument=Instrument::Saw;bass.gain=.40; bass.cutoff=.65;
 MidiRegion line;line.lengthBeats=16;
 int pitches[]={36,36,43,36,39,39,43,34};
 for(int i=0;i<16;++i)line.notes.push_back({pitches[(i/2)%8],double(i),.86f,.78f});
 bass.midi.push_back(line);s.tracks.push_back(bass);
 Track keys;keys.name="Electric Piano";keys.gain=.53;
 auto rhodes=readWav((inst/"Warm Electric Piano.wav").string());assignSamplerSample(keys,rhodes,60);
 MidiRegion harmony;harmony.lengthBeats=16;
 for(int bar=0;bar<4;++bar){int root[]={60,56,58,55};for(int p:{0,3,7,10})harmony.notes.push_back({root[bar]+p,double(bar*4),2.8,.55f});}
 keys.midi.push_back(harmony);keys.eq.bands[0].gainDb=-2;keys.delayMix=.12f;s.tracks.push_back(keys);
 Track lead;lead.name="Synth Lead";lead.gain=.33;lead.instrument=Instrument::Triangle;lead.pan=.25f;lead.delayMix=.17f;
 MidiRegion melody;melody.lengthBeats=16;
 const int notes[]={72,75,79,75,70,72,75,77,79,82,79,75,72,70,67,70};
 for(int i=0;i<16;++i)melody.notes.push_back({notes[i],double(i),.68f,(i%4==0?.86f:.7f)});
 lead.midi.push_back(melody);s.tracks.push_back(lead);
 Track pad;pad.name="Warm Pad";pad.gain=.24;
 auto padSample=readWav((inst/"Analog Soft Pad.wav").string());assignSamplerSample(pad,padSample,60);
 MidiRegion drone;drone.lengthBeats=16;
 drone.notes.push_back({48,0,7.7,.6f});drone.notes.push_back({55,0,7.7,.48f});drone.notes.push_back({51,8,7.7,.6f});drone.notes.push_back({58,8,7.7,.48f});
 pad.midi.push_back(drone);s.tracks.push_back(pad);
 auto rendered=s.render(8.5);writeWav(argv[3],rendered,s.sampleRate);
 std::cout<<"Rendered "<<rendered.size()/2<<" stereo frames / "<<s.tracks.size()<<" tracks\n";
}
