#include "ProductionWorkflow.h"
#include "MidiTools.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>

#define CHECK(x) do { if(!(x)){std::cerr<<"FAILED "<<#x<<" line "<<__LINE__<<"\n";std::abort();} }while(0)
int main(){
 using namespace veysona;
 for(auto kind:{workflow::Template::House, workflow::Template::DrumAndBass,workflow::Template::HipHop,workflow::Template::Ambient}){
  auto s=workflow::createTemplate(kind,24000);
  CHECK(s.bpm>=70&&s.bpm<180);CHECK(s.tracks.size()>=2);
  CHECK(std::all_of(s.tracks.begin(),s.tracks.end(),[](const Track& t){return !t.midi.empty()&&!t.midi.front().notes.empty();}));
  auto s2=workflow::createTemplate(kind,24000);
  CHECK(s.tracks[0].midi[0].notes[0].pitch==s2.tracks[0].midi[0].notes[0].pitch);
  EngineState state;state.prepare(s.tracks.size(),s.sampleRate);
  float left[512]{},right[512]{};renderTrackBlock(s,0,0,512,left,right,state);
  CHECK(std::all_of(left,left+512,[](float a){return std::isfinite(a);}));
  // Wavetable truly outputs MIDI notes, and the source is editable.
  for(size_t i=0;i<s.tracks.size();++i)if(s.tracks[i].instrument==Instrument::Wavetable){
   renderTrackBlock(s,i,0,512,left,right,state);
   CHECK(std::any_of(left,left+512,[](float a){return std::abs(a)>1e-5f;}));
  }
 }
 CHECK(workflow::createTemplate(workflow::Template::House,48000).tracks.size()==3);
 // Cubic curves retain endpoints and ease transitions without changing the
 // existing linear/step modes or the old on-disk project format.
 std::vector<AutomationLane> automation;
 setAutomationPoint(automation,AutomationTarget::TrackGain,0.,0.f);
 setAutomationPoint(automation,AutomationTarget::TrackGain,4.,1.f);
 CHECK(std::abs(automate(automation,AutomationTarget::TrackGain,1.,-2.f)-.25f)<1e-5f);
 automation[0].smooth=true;
 CHECK(std::abs(automate(automation,AutomationTarget::TrackGain,1.,-2.f)-.15625f)<1e-5f);
 CHECK(automate(automation,AutomationTarget::TrackGain,0.,-2.f)==0.f);
 CHECK(automate(automation,AutomationTarget::TrackGain,4.,-2.f)==1.f);

 auto s=workflow::createTemplate(workflow::Template::Ambient,24000);
 auto clip=workflow::bounceBuiltInTrack(s,0,6.);
 CHECK(clip.sampleRate==24000 && clip.stereo.size()>10000);
 CHECK(clip.stereo.size()%2==0);
 CHECK(!clip.waveformPeaks.empty());
 CHECK(std::any_of(clip.stereo.begin(),clip.stereo.end(),[](float a){return std::abs(a)>.0002f;}));
 CHECK(std::all_of(clip.stereo.begin(),clip.stereo.end(),[](float a){return std::isfinite(a);}));
 s.tracks[0].pluginChain.push_back({"vst3 external",{}});
 bool refused=false;try{workflow::bounceBuiltInTrack(s,0,1.);}catch(const std::invalid_argument&){refused=true;}
 CHECK(refused);
 Clip melody;melody.name="Mono C4 G4";melody.sampleRate=24000;
 const uint32_t rate=melody.sampleRate;
 for(int i=0;i<int(rate*1.6);++i){
  const double sec=double(i)/rate;
  const double frequency=sec<.7?261.625565:391.995436;
  const bool gated=(sec>=.10&&sec<.68)||(sec>=.82&&sec<1.45);
  const float a=gated?float(.35*std::sin(2*kPi*frequency*sec)):0.f;
  melody.stereo.push_back(a);melody.stereo.push_back(a);
 }
 auto region=workflow::transcribeMonophonic(melody,120.,.025f);
 CHECK(!region.notes.empty());
 bool c4=false,g4=false;
 for(const auto& n:region.notes){
  CHECK(n.pitch>=0&&n.pitch<=127&&n.beat>=0&&n.length>0);
  if(n.pitch==60)c4=true;
  if(n.pitch==67)g4=true;
 }
 CHECK(c4&&g4);
 auto quiet=melody;std::fill(quiet.stereo.begin(),quiet.stereo.end(),0.f);
 CHECK(workflow::transcribeMonophonic(quiet,120.).notes.empty());
 std::cout<<"v1.7 workflow: wavetable synthesis, four templates, native bounce, mono audio-to-MIDI passed\n";
}
