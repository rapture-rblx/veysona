#pragma once
// Eight-band equalizer workflow tools. JUCE-free so preset behaviour, history,
// and state validation can be tested without a Windows GUI.
#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

namespace veysona::eqtools {
enum class Preset { Flat, VocalPresence, BassWeight, DrumPunch, WarmKeys, AiryPads, DeMud, Telephone };
inline const char* presetName(Preset p) {
 switch(p) {
 case Preset::Flat:return "Flat / clean";
 case Preset::VocalPresence:return "Vocal presence";
 case Preset::BassWeight:return "Bass weight";
 case Preset::DrumPunch:return "Drum punch";
 case Preset::WarmKeys:return "Warm keys";
 case Preset::AiryPads:return "Airy pads";
 case Preset::DeMud:return "De-mud / clarity";
 case Preset::Telephone:return "Telephone / lo-fi";
 }
 return "Custom";
}
inline EqSettings makePreset(Preset p){
 EqSettings eq;
 // Preset application always resets all eight bands, avoiding stale EQ from
 // whatever patch was selected previously.
 switch(p){
 case Preset::Flat:break;
 case Preset::VocalPresence:
  eq.bands[0]={EqMode::LowShelf,110.f,-3.5f,.707f,true};
  eq.bands[1]={EqMode::Bell,350.f,-2.8f,1.1f,true};
  eq.bands[2]={EqMode::Bell,2800.f,2.8f,1.f,true};
  eq.bands[3]={EqMode::HighShelf,9800.f,2.2f,.707f,true};break;
 case Preset::BassWeight:
  eq.bands[0]={EqMode::LowShelf,85.f,4.4f,.707f,true};
  eq.bands[1]={EqMode::Bell,290.f,-3.f,1.3f,true};
  eq.bands[2]={EqMode::Bell,1600.f,1.2f,1.f,true};
  eq.bands[3]={EqMode::HighShelf,8200.f,-2.5f,.707f,true};break;
 case Preset::DrumPunch:
  eq.bands[0]={EqMode::LowShelf,90.f,2.f,.707f,true};
  eq.bands[1]={EqMode::Bell,420.f,-2.6f,1.3f,true};
  eq.bands[2]={EqMode::Bell,3800.f,3.f,1.2f,true};
  eq.bands[3]={EqMode::HighShelf,11000.f,1.8f,.707f,true};break;
 case Preset::WarmKeys:
  eq.bands[0]={EqMode::LowShelf,155.f,1.8f,.707f,true};
  eq.bands[1]={EqMode::Bell,610.f,-1.8f,1.1f,true};
  eq.bands[2]={EqMode::Bell,2500.f,-1.4f,1.0f,true};
  eq.bands[3]={EqMode::HighShelf,8800.f,-1.5f,.707f,true};break;
 case Preset::AiryPads:
  eq.bands[0]={EqMode::LowShelf,135.f,-2.5f,.707f,true};
  eq.bands[1]={EqMode::Bell,500.f,-1.5f,1.f,true};
  eq.bands[2]={EqMode::Bell,3200.f,-1.7f,1.2f,true};
  eq.bands[3]={EqMode::HighShelf,11800.f,3.2f,.707f,true};break;
 case Preset::DeMud:
  eq.bands[0]={EqMode::LowShelf,85.f,-1.3f,.707f,true};
  eq.bands[1]={EqMode::Bell,340.f,-4.2f,1.4f,true};
  eq.bands[2]={EqMode::Bell,2600.f,1.4f,1.1f,true};
  eq.bands[3]={EqMode::HighShelf,8500.f,1.2f,.707f,true};break;
 case Preset::Telephone:
  eq.bands[0]={EqMode::LowShelf,280.f,-16.f,.85f,true};
  eq.bands[1]={EqMode::Bell,600.f,3.6f,1.1f,true};
  eq.bands[2]={EqMode::Bell,1750.f,4.4f,1.5f,true};
  eq.bands[3]={EqMode::HighShelf,4100.f,-16.f,.85f,true};break;
 }
 eq.enabled=true;
 return eq;
}
inline void sanitize(EqSettings& eq){
 const EqSettings defaults;
 for(size_t i=0;i<eq.bands.size();++i){
  auto& b=eq.bands[i];const auto& def=defaults.bands[i];
  b.frequency=std::isfinite(b.frequency)?std::clamp(b.frequency,30.f,20000.f):def.frequency;
  b.gainDb=std::isfinite(b.gainDb)?std::clamp(b.gainDb,-18.f,18.f):def.gainDb;
  b.q=std::isfinite(b.q)?std::clamp(b.q,.25f,12.f):def.q;
 }
}
inline bool equal(const EqSettings& a,const EqSettings& b){
 if(a.enabled!=b.enabled)return false;
 for(size_t i=0;i<a.bands.size();++i){
  const auto& x=a.bands[i];const auto& y=b.bands[i];
  if(x.enabled!=y.enabled||x.mode!=y.mode||std::abs(x.frequency-y.frequency)>0.0001f||
     std::abs(x.gainDb-y.gainDb)>0.0001f||std::abs(x.q-y.q)>0.0001f)return false;
 }
 return true;
}
class History {
 struct Change {int track;EqSettings before,after;};
 std::vector<Change> undoStack,redoStack;
 static constexpr size_t maxChanges=128;
 static bool apply(std::vector<Change>& from,std::vector<Change>& to,int track,EqSettings& current,bool backward){
  for(auto it=from.rbegin();it!=from.rend();++it){
   if(it->track!=track)continue;
   auto change=*it;
   from.erase(std::next(it).base());
   current=backward?change.before:change.after;
   to.push_back(std::move(change));
   return true;
  }
  return false;
 }
public:
 void clear(){undoStack.clear();redoStack.clear();}
 void commit(int track,const EqSettings& before,const EqSettings& after){
  if(track<0||equal(before,after))return;
  undoStack.push_back({track,before,after});
  if(undoStack.size()>maxChanges)undoStack.erase(undoStack.begin());
  redoStack.clear();
 }
 bool undo(int track,EqSettings& current){return apply(undoStack,redoStack,track,current,true);}
 bool redo(int track,EqSettings& current){return apply(redoStack,undoStack,track,current,false);}
 bool canUndo(int track) const {for(const auto& x:undoStack)if(x.track==track)return true;return false;}
 bool canRedo(int track) const {for(const auto& x:redoStack)if(x.track==track)return true;return false;}
};
} // namespace veysona::eqtools
