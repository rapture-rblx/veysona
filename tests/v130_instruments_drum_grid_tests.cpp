#include "AudioCore.h"
#include "DrumSequencer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static void check(bool good,const char* why){if(!good)throw std::runtime_error(why);}
static float maxAbs(const std::array<float,2>& sample){return std::max(std::abs(sample[0]),std::abs(sample[1]));}
int main(){
 using namespace veysona;
 MidiRegion region; region.lengthBeats=4;
 check(drums::setStep(region,36,0,true,.8f),"step insertion");
 check(drums::findNote(region,36,0)==0,"step lookup");
 check(region.notes[0].channel==10,"drum notes use MIDI channel 10");
 check(std::abs(region.notes[0].beat)<1e-7,"step-0 beat location");
 check(drums::setStep(region,38,4,true,.65f),"snare step insertion");
 check(std::abs(region.notes[1].beat-1.)<1e-6,"sixteenth-note timing");
 check(drums::setVelocity(region,38,4,.95f),"velocity edit");
 check(std::abs(region.notes[1].velocity-.95f)<1e-6,"velocity value retained");
 check(!drums::toggleStep(region,38,4),"toggle-off result");
 check(drums::findNote(region,38,4)==-1,"toggle-off removal");
 check(drums::toggleStep(region,42,63),"four-bar placement");
 check(region.lengthBeats>=16.,"length expands to four bars");
 check(!drums::setStep(region,128,0,true),"invalid pitch rejected");
 check(!drums::setStep(region,36,-3,true),"negative step rejected");
 Track drumsTrack;drumsTrack.instrument=Instrument::DrumRack;
 drumsTrack.midi.push_back(region);
 Session drumSession;drumSession.tracks.push_back(drumsTrack);
 auto drumAudio=renderMidiRegionSample(drumSession,drumsTrack,region,.005);
 check(maxAbs(drumAudio)>0.0001f,"legacy GM drum racks preserve fallback for old projects");
 drumsTrack.drumRackRecordedOnly=true;
 check(maxAbs(renderMidiRegionSample(drumSession,drumsTrack,region,.005))==0.f,
       "recorded-only drum racks never fabricate replacement sounds");
 Clip hit;hit.name="unit-test hit";hit.sampleRate=44100;hit.stereo.assign(22050*2,.0f);
 hit.stereo[0]=.7f;hit.stereo[1]=.7f;
 assignDrumSample(drumsTrack,0,36,hit);
 const auto recorded=renderMidiRegionSample(drumSession,drumsTrack,region,0.000005);
 check(maxAbs(recorded)>0.0001f,"recorded drum pads render assigned audio");
 Track keys;
 std::vector<SamplerZone> zones;
 for(int root:{48,60,72}){
  SamplerZone z;z.rootNote=root;z.lowNote=root-6;z.highNote=root+5;z.sampleRate=44100;
  z.name="zone";z.stereo.resize(44100*2);
  const float amplitude=root==48?.15f:(root==60?.30f:.45f);
  for(size_t i=0;i<44100;++i){z.stereo[i*2]=amplitude;z.stereo[i*2+1]=amplitude;}
  zones.push_back(std::move(z));
 }
 assignSamplerZones(keys,std::move(zones),"Test Piano");
 check(keys.instrument==Instrument::Sampler,"multisample chooses sampler");
 check(keys.sampleZones.size()==3,"all sample zones retained");
 Session session;session.tracks.push_back(keys);
 auto play=[&](int pitch){MidiRegion r;r.lengthBeats=4;r.notes.push_back({pitch,0,2,1.f});return renderMidiRegionSample(session,keys,r,.06);};
 check(play(48)[0]<play(60)[0]&&play(60)[0]<play(72)[0],"nearest multisample keyzone used for pitch");
 check(maxAbs(play(48))>0.001f,"sampler audible at C3");
 // Multiple genuine sample layers are switched by note velocity, not merely amplified.
 Track expressive;
 std::vector<SamplerZone> layers;
 for(int i=0;i<2;++i){
  SamplerZone z;z.rootNote=60;z.lowNote=0;z.highNote=127;z.sampleRate=44100;
  z.velocityLow=float(i)*.5f;z.velocityHigh=float(i+1)*.5f;
  z.stereo.assign(44100*2,i==0?.11f:.63f);
  layers.push_back(std::move(z));
 }
 assignSamplerZones(expressive,std::move(layers),"Velocity demo");
 auto layerOutput=[&](float v){MidiRegion r;r.lengthBeats=4;r.notes.push_back({60,0,1,v});return renderMidiRegionSample(session,expressive,r,.06)[0];};
 const auto quiet=layerOutput(.35f),loud=layerOutput(.82f);
 check(loud>quiet*6.f,"velocity switches distinct sample recordings instead of scaling one source");
 check(maxAbs(play(84))>0.001f,"out-of-zone pitch uses nearest sample");
 Clip mono;mono.sampleRate=44100;mono.stereo.assign(44100*2,.2f);mono.name="single";
 assignSamplerSample(keys,mono,60);
 check(keys.sampleZones.empty(),"switching to single sampler clears zone map");
 check(maxAbs(play(60))>0.001f,"legacy sampler playback works");
#ifdef VEYSONA_SOURCE_PATH
 fs::path root(VEYSONA_SOURCE_PATH);
 check(fs::exists(root/"tools/install_recorded_instruments.py"),"recorded sample installer missing");
 check(fs::exists(root/"RECORDED_INSTRUMENTS.md"),"recorded sample licence/provenance instructions missing");
 check(fs::is_directory(root/"assets/instrument_packs"),"recorded instrument pack folder missing");
 check(fs::is_directory(root/"assets/drum_kits"),"recorded drum folder missing");
 for(const auto& folder: {root/"assets/instrument_packs",root/"assets/drum_kits"}){
  for(const auto& e:fs::directory_iterator(folder)){
   if(!e.is_directory())continue;
   check(fs::exists(e.path()/"RECORDED_SOURCE.json"),"unverified instrument directory must not be labeled recorded");
  }
 }
#endif
 std::cout<<"v1.30: drum steps, legacy fallback, recorded-only safety and sampled zones OK\n";
}
