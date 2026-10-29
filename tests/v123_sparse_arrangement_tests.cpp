#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace veysona;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<" "<<#x<<'\n';return 1;}}while(false)
int main(){
 Session s;s.sampleRate=48000;s.bpm=120;s.master.gain=1;s.master.limiterEnabled=false;
 Track t;t.gain=1;t.instrument=Instrument::Sine;
 for(int i=0;i<1800;++i){
  Clip c;c.sampleRate=(i%3==0)?44100:48000;c.startBeat=i*32.;c.gain=.4f;
  c.stereo.resize(size_t(c.sampleRate)/50*2);
  for(size_t f=0;f<c.stereo.size()/2;++f){c.stereo[2*f]=.5f*std::sin(2*kPi*440*f/c.sampleRate);c.stereo[2*f+1]=-.2f*std::cos(2*kPi*440*f/c.sampleRate);}
  t.clips.push_back(std::move(c));
 }
 s.tracks.push_back(std::move(t));EngineState state;state.prepare(1,s.sampleRate);
 std::vector<float> left(512),right(512);
 auto verify=[&](int64_t pos){
  renderTrackBlock(s,0,pos,512,left.data(),right.data(),state,false,false);
  for(int i=0;i<512;++i){
   double expL=0,expR=0;
   for(const auto& c:s.tracks[0].clips){
    auto start=int64_t(std::llround(c.startBeat*60./s.bpm*s.sampleRate));auto rel=pos+i-start;if(rel<0)continue;
    double source=double(rel)*c.sampleRate/s.sampleRate;size_t idx=size_t(source);size_t total=c.stereo.size()/2;
    if(idx>=total)continue;
    float f=float(source-idx);auto next=std::min(idx+1,total-1);
    expL+=((1-f)*c.stereo[2*idx]+f*c.stereo[2*next])*c.gain;
    expR+=((1-f)*c.stereo[2*idx+1]+f*c.stereo[2*next+1])*c.gain;
   }
   CHECK(std::abs(left[i]-expL)<2e-6);CHECK(std::abs(right[i]-expR)<2e-6);
  }
  return 1;
 };
 CHECK(verify(0));CHECK(verify(48000*16-256));CHECK(verify(48000*16));CHECK(verify(48000*17));
 CHECK(verify(48000*16*1799));CHECK(verify(48000*16*1799+250));
 std::cout<<"PASS: distant clip culling preserves exact overlapping stereo audio at 44.1/48 kHz\n";
}
