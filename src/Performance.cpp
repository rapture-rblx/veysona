#include "Performance.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace veysona {
namespace {
double boundary(double now,double grid){
 if(!std::isfinite(now)||now<0)now=0;
 const double nearest=std::round(now/grid)*grid;
 if(std::abs(nearest-now)<1e-8)return std::max(0.,nearest);
 return std::ceil(now/grid)*grid;
}
float sampleChannel(const Clip& clip,double elapsedSec,int channel){
 const size_t count=clip.stereo.size()/2;
 if(count==0||clip.sampleRate<8000)return 0;
 const double position=std::fmod(elapsedSec*clip.sampleRate,double(count));
 if(!std::isfinite(position))return 0;
 auto a=size_t(std::max(0.,position));a=std::min(a,count-1);
 const auto b=(a+1)%count;
 const float frac=float(position-a);
 const float audio=clip.stereo[2*a+channel]*(1-frac)+clip.stereo[2*b+channel]*frac;
 // Apply fades at loop boundaries. This is intentionally a looping audio clip,
 // not a crossfade into the following arrangement clip.
 const double duration=double(count)/clip.sampleRate;
 const float fadeIn=clip.fadeInSeconds>0?float(std::clamp((position/clip.sampleRate)/clip.fadeInSeconds,0.,1.)):1.f;
 const float fadeOut=clip.fadeOutSeconds>0?float(std::clamp((duration-position/clip.sampleRate)/clip.fadeOutSeconds,0.,1.)):1.f;
 return audio*clip.gain*(clip.equalPowerFades?std::sin(fadeIn*float(kPi/2))*std::sin(fadeOut*float(kPi/2)):fadeIn*fadeOut);
}
}
std::size_t commitPerformanceScene(Session& session,size_t scene,double startBeat,double lengthBeats){
 if(!std::isfinite(startBeat)||!std::isfinite(lengthBeats)||startBeat<0||lengthBeats<=0||lengthBeats>128||session.bpm<=0)
  throw std::invalid_argument("Invalid scene capture time");
 size_t count=0;
 for(auto& track:session.tracks){
  if(scene<track.clips.size()){
   auto output=track.clips[scene];
   if(output.stereo.size()<2||output.sampleRate<8000)continue;
   const auto srcFrames=output.stereo.size()/2;
   const double seconds=lengthBeats*60./session.bpm;
   if(seconds*output.sampleRate>16'000'000)throw std::runtime_error("Captured clip exceeds 16 million frames");
   const auto frames=size_t(std::max<int64_t>(1,std::llround(seconds*output.sampleRate)));
   std::vector<float> audio(frames*2);
   for(size_t f=0;f<frames;++f){const auto src=(f%srcFrames)*2;audio[f*2]=output.stereo[src];audio[f*2+1]=output.stereo[src+1];}
   output.stereo.swap(audio);output.startBeat=startBeat;
   output.name+=" (captured scene "+std::to_string(scene+1)+")";
   buildWaveformPeaks(output);
   track.clips.push_back(std::move(output));++count;
  }else if(scene-track.clips.size()<track.midi.size()){
   auto region=track.midi[scene-track.clips.size()];
   if(region.lengthBeats<=0)continue;
   MidiRegion copied;copied.name=region.name+" (captured scene "+std::to_string(scene+1)+")";
   copied.startBeat=startBeat;copied.lengthBeats=lengthBeats;
   for(double cycle=0;cycle<lengthBeats-1e-8;cycle+=region.lengthBeats){
    for(auto note:region.notes){note.beat+=cycle;if(note.beat>=lengthBeats)continue;
     note.length=std::min(note.length,lengthBeats-note.beat);copied.notes.push_back(note);}
    for(auto control:region.controllers){control.beat+=cycle;
     if(control.beat<lengthBeats)copied.controllers.push_back(control);}
   }
   track.midi.push_back(std::move(copied));++count;
  }
 }
 return count;
}
void PerformanceLauncher::prepare(std::size_t tracks){
 if(tracks>256)throw std::invalid_argument("Performance mode supports up to 256 tracks");
 tracks_.assign(tracks,{});
}
void PerformanceLauncher::clear() noexcept {for(auto& s:tracks_)s={};}
void PerformanceLauncher::setQuantisation(double beats){
 if(!std::isfinite(beats)||beats<0||beats>64)throw std::invalid_argument("Invalid launch quantisation");
 quantisationBeats_=beats;
}
bool PerformanceLauncher::isRunning() const noexcept {
 return std::any_of(tracks_.begin(),tracks_.end(),[](const auto& t){return t.active.kind!=LaunchKind::Stop||t.hasQueue;});
}
const PerformanceTrackState& PerformanceLauncher::state(std::size_t track) const{
 if(track>=tracks_.size())throw std::out_of_range("Performance track not prepared");
 return tracks_[track];
}
void PerformanceLauncher::queue(std::size_t track,LaunchKind kind,int index,double currentBeat){
 if(track>=tracks_.size())throw std::out_of_range("No such performance track");
 if(kind!=LaunchKind::Stop&&index<0)throw std::invalid_argument("Invalid clip index");
 auto& t=tracks_[track];
 t.hasQueue=true;t.queued={kind,kind==LaunchKind::Stop?-1:index,0};
 t.queuedForBeat=quantisationBeats_==0?std::max(0.,currentBeat):boundary(currentBeat,quantisationBeats_);
}
void PerformanceLauncher::queueScene(const Session& session,std::size_t scene,double currentBeat){
 for(size_t i=0;i<std::min(tracks_.size(),session.tracks.size());++i){
  const auto& t=session.tracks[i];
  if(scene<t.clips.size())queue(i,LaunchKind::Audio,int(scene),currentBeat);
  else if(scene-t.clips.size()<t.midi.size())queue(i,LaunchKind::Midi,int(scene-t.clips.size()),currentBeat);
  else queue(i,LaunchKind::Stop,-1,currentBeat);
 }
}
void PerformanceLauncher::stopAll(double currentBeat){
 for(size_t i=0;i<tracks_.size();++i)queue(i,LaunchKind::Stop,-1,currentBeat);
}
void PerformanceLauncher::renderTrack(const Session& session,size_t track,int64_t position,int frames,float* left,float* right,bool builtInMidi){
 if(!left||!right||frames<=0)return;
 std::fill_n(left,frames,0.f);std::fill_n(right,frames,0.f);
 if(track>=tracks_.size()||track>=session.tracks.size()||session.bpm<=0||session.sampleRate<8000)return;
 auto& state=tracks_[track];const auto& tr=session.tracks[track];
 const bool anySolo=std::any_of(session.tracks.begin(),session.tracks.end(),[](const auto& t){return t.solo;});
 const bool audible=!tr.mute&&(!anySolo||tr.solo);
 const double beatPerFrame=session.bpm/(60.*session.sampleRate);
 for(int i=0;i<frames;++i){
  const double beat=double(position+i)*beatPerFrame;
  if(state.hasQueue&&beat+1e-9>=state.queuedForBeat){
   state.active=state.queued;
   state.active.beganAtBeat=state.queuedForBeat;
   state.hasQueue=false;
  }
  if(!audible||state.active.kind==LaunchKind::Stop)continue;
  const auto& a=state.active;
  const double elapsed=std::max(0.,(beat-a.beganAtBeat)*60./session.bpm);
  if(a.kind==LaunchKind::Audio){
   if(a.index<0||size_t(a.index)>=tr.clips.size())continue;
   const auto& clip=tr.clips[size_t(a.index)];
   if(clip.mute)continue;
   left[i]=sampleChannel(clip,elapsed,0);
   right[i]=sampleChannel(clip,elapsed,1);
  }else if(a.kind==LaunchKind::Midi&&builtInMidi){
   if(a.index<0||size_t(a.index)>=tr.midi.size())continue;
   const auto& region=tr.midi[size_t(a.index)];
   if(region.lengthBeats<=0)continue;
   const auto localBeat=std::fmod(beat-a.beganAtBeat,region.lengthBeats);
   const auto sample=renderMidiRegionSample(session,tr,region,localBeat);
   left[i]=sample[0];right[i]=sample[1];
  }
 }
}
int PerformanceLauncher::collectMidiEvents(const Session& session,size_t track,int64_t position,
                                           int frames,PerformanceMidiEvent* out,int capacity) const{
 if(!out||capacity<=0||frames<=0||track>=tracks_.size()||track>=session.tracks.size()
    ||session.bpm<=0||session.sampleRate<8000)return 0;
 const auto& state=tracks_[track];const auto& instrument=session.tracks[track];
 const double beatsPerSample=session.bpm/(60.*session.sampleRate);
 const double begin=double(position)*beatsPerSample,end=double(position+frames)*beatsPerSample;
 int count=0;
 auto push=[&](PerformanceMidiType type,double beat,int channel,int number,int value){
  if(beat<begin-1e-8||beat>=end-1e-8||count>=capacity)return;
  const auto sample=int(std::llround((beat-begin)/beatsPerSample));
  if(sample<0||sample>=frames)return;
  out[count++]={type,sample,std::clamp(channel,1,16),number,value};
 };
 auto regionEvents=[&](const LaunchSlot& launched,double start,double finish){
  if(launched.kind!=LaunchKind::Midi||launched.index<0||size_t(launched.index)>=instrument.midi.size())return;
  const auto& region=instrument.midi[size_t(launched.index)];
  const double length=region.lengthBeats;
  if(!std::isfinite(length)||length<.125)return;
  auto sequence=[&](double localBeat,PerformanceMidiType type,int channel,int number,int value){
   const double first=launched.beganAtBeat+localBeat;
   const int64_t k0=std::max(int64_t(0),int64_t(std::floor((start-first)/length)));
   const int64_t k1=std::max(k0,int64_t(std::ceil((finish-first)/length)));
   for(int64_t k=k0;k<=std::min(k1,k0+1024)&&count<capacity;++k){
    const double beat=first+k*length;
    if(beat>=start-1e-8&&beat<finish-1e-8)push(type,beat,channel,number,value);
   }
  };
  for(const auto& n:region.notes){
   sequence(n.beat,PerformanceMidiType::NoteOn,n.channel,std::clamp(n.pitch,0,127),std::clamp(int(n.velocity*127),1,127));
   sequence(n.beat+std::max(.0001,n.length),PerformanceMidiType::NoteOff,n.channel,std::clamp(n.pitch,0,127),0);
  }
  for(const auto& controller:region.controllers){
   const auto type=controller.type==MidiControlType::CC?PerformanceMidiType::CC:
    controller.type==MidiControlType::PitchWheel?PerformanceMidiType::PitchWheel:PerformanceMidiType::Pressure;
   sequence(controller.beat,type,controller.channel,controller.number,controller.value);
  }
 };
 if(state.hasQueue&&state.queuedForBeat<end){
  // Stop any sounding VST voices on scene changes before sending the new clip.
  regionEvents(state.active,begin,std::max(begin,state.queuedForBeat));
  if(state.active.kind==LaunchKind::Midi){
   for(int channel=1;channel<=16;++channel)push(PerformanceMidiType::AllNotesOff,state.queuedForBeat,channel,0,0);
  }
  auto queued=state.queued;queued.beganAtBeat=state.queuedForBeat;
  regionEvents(queued,std::max(begin,state.queuedForBeat),end);
 }else regionEvents(state.active,begin,end);
 // Insertion sort is bounded by the supplied scratch capacity; no heap use.
 auto priority=[](PerformanceMidiType type){return type==PerformanceMidiType::AllNotesOff?0:type==PerformanceMidiType::NoteOff?1:2;};
 for(int i=1;i<count;++i){
  auto value=out[i];int j=i-1;
  while(j>=0 && (out[j].offset>value.offset ||
          (out[j].offset==value.offset&&priority(out[j].type)>priority(value.type)))){
   out[j+1]=out[j];--j;
  }
  out[j+1]=value;
 }
 return count;
}

}
