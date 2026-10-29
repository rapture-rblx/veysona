#include "../src/EqWorkflow.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>

int main(){
 using namespace veysona;
 using namespace veysona::eqtools;
 const Preset presets[]={Preset::Flat,Preset::VocalPresence,Preset::BassWeight,Preset::DrumPunch,
                          Preset::WarmKeys,Preset::AiryPads,Preset::DeMud,Preset::Telephone};
 std::set<int> fingerprints;
 for(auto preset:presets){
  EqSettings value=makePreset(preset);
  assert(value.enabled);
  for(const auto& band:value.bands){
   assert(band.frequency>=30.f&&band.frequency<=20000.f);
   assert(band.gainDb>=-18.f&&band.gainDb<=18.f);
   assert(band.q>=.25f&&band.q<=12.f);
  }
  // Every preset must remain acoustically meaningful and numerically stable.
  double accumulator=0;
  for(int i=0;i<101;i++){
   const double frequency=30.*std::pow(20000./30.,double(i)/100.);
   const double db=eqMagnitudeDb(value,frequency,48000);
   assert(std::isfinite(db));
   accumulator+=db*(i+1);
  }
  fingerprints.insert(int(std::round(accumulator*100.)));
 }
 assert(fingerprints.size()==8); // no duplicate sound curves
 {
  auto eq=makePreset(Preset::DrumPunch);
  eq.bands[4].gainDb=8.2f;
  auto flat=makePreset(Preset::Flat);
  assert(flat.bands[4].gainDb==0.f); // presets reset all eight bands
  assert(!equal(eq,flat));
 }
 {
  EqSettings malformed;
  malformed.bands[0].frequency=std::numeric_limits<float>::quiet_NaN();
  malformed.bands[0].q=std::numeric_limits<float>::infinity();
  malformed.bands[1].gainDb=-120;
  malformed.bands[2].frequency=800000;
  sanitize(malformed);
  assert(std::isfinite(malformed.bands[0].frequency));
  assert(std::isfinite(malformed.bands[0].q));
  assert(malformed.bands[1].gainDb==-18.f);
  assert(malformed.bands[2].frequency==20000.f);
 }
 {
  History history;
  auto flat=makePreset(Preset::Flat);
  auto vocal=makePreset(Preset::VocalPresence);
  auto bass=makePreset(Preset::BassWeight);
  auto current=flat;
  history.commit(0,current,vocal);current=vocal;
  history.commit(1,flat,bass);
  assert(history.undo(0,current)&&equal(current,flat));
  assert(history.redo(0,current)&&equal(current,vocal));
  assert(!history.undo(4,current));
  history.commit(0,current,bass);current=bass;
  assert(!history.canRedo(0));
  assert(history.undo(0,current)&&equal(current,vocal));
  history.clear();assert(!history.canUndo(0));
  history.commit(0,current,current);assert(!history.canUndo(0));
 }
 std::cout<<"Veysona v1.14 EQ preset, validation and history tests passed\n";
}
