#pragma once
// Drum-grid editing is pure MIDI editing: the UI and engine use the same notes.
// No locks, GUI dependencies or allocations in the audio callback.
#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace veysona::drums {
constexpr int kPads=8;
constexpr int kStepsPerBar=16;
constexpr double kStepBeats=.25;
inline double beatAt(int absoluteStep){ return std::max(0,absoluteStep)*kStepBeats; }
inline int stepAt(double beat){ return std::max(0,int(std::lround(beat/kStepBeats))); }
inline int findNote(const MidiRegion& region,int pitch,int absoluteStep){
 const double target=beatAt(absoluteStep);
 for(int i=0;i<int(region.notes.size());++i)
  if(region.notes[size_t(i)].pitch==pitch && std::abs(region.notes[size_t(i)].beat-target)<.00001)
   return i;
 return -1;
}
inline bool setStep(MidiRegion& region,int pitch,int absoluteStep,bool enabled,float velocity=.8f){
 if(pitch<0||pitch>127||absoluteStep<0||absoluteStep>4095)return false;
 const int existing=findNote(region,pitch,absoluteStep);
 if(enabled){
  if(existing>=0){region.notes[size_t(existing)].velocity=std::clamp(velocity,.01f,1.f);return false;}
  region.notes.push_back({pitch,beatAt(absoluteStep),.125f,std::clamp(velocity,.01f,1.f),10});
  region.lengthBeats=std::max(region.lengthBeats,std::ceil((beatAt(absoluteStep)+kStepBeats)/4.)*4.);
  return true;
 }
 if(existing<0)return false;
 region.notes.erase(region.notes.begin()+existing);
 return true;
}
inline bool toggleStep(MidiRegion& region,int pitch,int absoluteStep,float velocity=.8f){
 const bool wasActive=findNote(region,pitch,absoluteStep)>=0;
 setStep(region,pitch,absoluteStep,!wasActive,velocity);
 return !wasActive;
}
inline bool setVelocity(MidiRegion& region,int pitch,int absoluteStep,float velocity){
 const auto idx=findNote(region,pitch,absoluteStep);
 if(idx<0)return false;
 region.notes[size_t(idx)].velocity=std::clamp(velocity,.01f,1.f);
 return true;
}
}
