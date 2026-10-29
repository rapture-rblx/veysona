#include "AudioCore.h"
#include <filesystem>
#include <iostream>
using namespace veysona;
int main(int argc,char**argv){
 if(argc!=4){std::cerr<<"Usage: veysona_v09_demo <samples> <instruments> <output.wav>\n";return 2;}
 std::filesystem::path samples=argv[1], inst=argv[2];
 Session s;s.bpm=120;s.sampleRate=48000;s.master.gain=.78f;s.master.limiterEnabled=true;
 s.buses.push_back({"Drum Group",false,.92f,0,1,{}});
 s.buses.push_back({"Bass Group",false,.9f,0,1,{}});
 s.buses.push_back({"FX Send",false,.40f,.14f,1.45f,{}});
 Track drums;drums.name="Drum Rack";drums.gain=.82f;drums.outputBus=0;
 auto drum=[&](int slot,int note,const char* name){auto c=readWav((samples/name).string());assignDrumSample(drums,size_t(slot),note,c);};
 drum(0,36,"Kick - Deep.wav");drum(1,38,"Snare - Clean.wav");
 drum(2,42,"HiHat - Closed.wav");drum(3,46,"HiHat - Open.wav");
 MidiRegion kit;kit.lengthBeats=32;
 for(int bar=0;bar<8;++bar)for(int step=0;step<8;++step){
  const double beat=bar*4.+step*.5;
  kit.notes.push_back({42,beat,.125f,step%2?.32f:.58f});
  if(step==0||step==4)kit.notes.push_back({36,beat,.125f,.95f});
  if(step==2||step==6)kit.notes.push_back({38,beat,.125f,.78f});
 }
 drums.midi.push_back(kit);s.tracks.push_back(drums);
 Track bass;bass.name="Analog Bass";bass.instrument=Instrument::Saw;bass.outputBus=1;
 bass.gain=.44f;bass.cutoff=.47f;bass.compressor.enabled=true;
 MidiRegion line;line.lengthBeats=32;
 const int pitches[]={36,36,43,36,39,39,43,34};
 for(int i=0;i<32;++i)line.notes.push_back({pitches[(i/2)%8],double(i),.85f,.83f});
 bass.midi.push_back(line);s.tracks.push_back(bass);
 Track keys;keys.name="Warm Electric Piano";keys.gain=.48f;keys.sends[2]=.25f;
 assignSamplerSample(keys,readWav((inst/"Warm Electric Piano.wav").string()),60);
 keys.eq.bands[0].gainDb=-3.f;keys.delayMix=.08f;
 MidiRegion chords;chords.lengthBeats=32;
 for(int bar=0;bar<8;++bar){int root[]={60,56,58,55};for(int p:{0,3,7,10})
  chords.notes.push_back({root[bar%4]+p,double(bar*4),2.9,.52f});}
 keys.midi.push_back(chords);
 setAutomationPoint(keys.automation,AutomationTarget::TrackGain,0,0.f);
 setAutomationPoint(keys.automation,AutomationTarget::TrackGain,4,.48f);
 setAutomationPoint(keys.automation,AutomationTarget::TrackGain,28,.48f);
 setAutomationPoint(keys.automation,AutomationTarget::TrackGain,32,0.f);
 s.tracks.push_back(keys);
 Track lead;lead.name="Prism Lead";lead.instrument=Instrument::Triangle;lead.gain=.38f;
 lead.delayMix=.17f;lead.sends[2]=.28f;
 MidiRegion melody;melody.lengthBeats=32;
 const int notes[]={72,75,79,75,70,72,75,77,79,82,79,75,72,70,67,70};
 for(int i=0;i<32;++i)melody.notes.push_back({notes[i%16],double(i),.66f,(i%4==0?.86f:.70f)});
 lead.midi.push_back(melody);
 setAutomationPoint(lead.automation,AutomationTarget::TrackPan,0,-.6f);
 setAutomationPoint(lead.automation,AutomationTarget::TrackPan,16,.7f);
 setAutomationPoint(lead.automation,AutomationTarget::TrackPan,32,-.6f);
 s.tracks.push_back(lead);
 Track pad;pad.name="Analog Soft Pad";pad.gain=.23f;pad.stereoWidth=1.48f;pad.sends[2]=.2f;
 assignSamplerSample(pad,readWav((inst/"Analog Soft Pad.wav").string()),60);
 MidiRegion drone;drone.lengthBeats=32;
 for(int bar=0;bar<4;++bar){drone.notes.push_back({48+bar%2*3,double(bar*8),7.3,.5f});
 drone.notes.push_back({55+bar%2*3,double(bar*8),7.3,.4f});}
 pad.midi.push_back(drone);s.tracks.push_back(pad);
 auto rendered=s.render(16.3);writeWav(argv[3],rendered,s.sampleRate);
 std::cout<<"v0.9: "<<s.tracks.size()<<" tracks, "<<s.buses.size()<<" buses, automation, MIDI sampler/drum rack, "<<rendered.size()/2<<" frames\n";
}
