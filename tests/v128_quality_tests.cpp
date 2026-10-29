#include "AudioCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace veysona;
static void check(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
static void near(float a,float b,float tolerance=1.e-5f){check(std::abs(a-b)<=tolerance,"Audio sample mismatch");}

static void eightBusRouting(){
 Session session;session.buses.resize(kMaxBuses);
 check(kMaxBuses==8,"Eight buses must be enabled");
 Track tr;tr.outputBus=7;tr.sends[7]=1.f;tr.sends[2]=.25f;
 std::array<float,4> sourceL{.4f,.2f,-.5f,1.f};
 std::array<float,4> sourceR{-.2f,.1f,.75f,.5f};
 std::array<float,4> masterL{},masterR{};
 std::array<std::array<float,4>,kMaxBuses> busL{},busR{};
 std::array<float*,kMaxBuses> pL{},pR{};
 for(size_t b=0;b<kMaxBuses;++b){pL[b]=busL[b].data();pR[b]=busR[b].data();}
 routeTrack(tr,4,sourceL.data(),sourceR.data(),masterL.data(),masterR.data(),pL.data(),pR.data(),session.buses.size());
 for(int i=0;i<4;++i){
  near(masterL[i],0.f);near(masterR[i],0.f);
  near(busL[7][i],sourceL[i]);near(busR[7][i],sourceR[i]); // no double-route on send to own output
  near(busL[2][i],sourceL[i]*.25f);near(busR[2][i],sourceR[i]*.25f);
 }
 session.buses[7].muted=true;
 applyBus(session.buses[7],session,0,4,busL[7].data(),busR[7].data());
 for(int i=0;i<4;++i){near(busL[7][i],0);near(busR[7][i],0);}
 // No bus arrays: invalid routing must fall back safely to main output.
 std::fill(masterL.begin(),masterL.end(),0.f);
 std::fill(masterR.begin(),masterR.end(),0.f);
 routeTrack(tr,4,sourceL.data(),sourceR.data(),masterL.data(),masterR.data(),nullptr,nullptr,session.buses.size());
 for(int i=0;i<4;++i){near(masterL[i],sourceL[i]);near(masterR[i],sourceR[i]);}
}
static void soloCacheEquivalent(){
 Session s;s.sampleRate=48000;s.bpm=120;s.tracks.resize(12);
 EngineState oldState,newState;oldState.prepare(s.tracks.size(),s.sampleRate);newState.prepare(s.tracks.size(),s.sampleRate);
 for(size_t i=0;i<s.tracks.size();++i){
  auto& tr=s.tracks[i];tr.name="Quality Track";
  Clip c;c.sampleRate=s.sampleRate;c.startBeat=0;
  c.stereo.assign(2048,0);
  for(size_t f=0;f<1024;++f){c.stereo[2*f]=float(.1*std::sin(.01*f+i));c.stereo[2*f+1]=float(.15*std::cos(.01*f+i));}
  tr.clips.push_back(std::move(c));
  tr.solo=(i%4==2);
 }
 for(bool soloMode:{false,true}){
  if(!soloMode)for(auto& tr:s.tracks)tr.solo=false;
  else for(size_t i=0;i<s.tracks.size();++i)s.tracks[i].solo=i%4==2;
  const bool anySolo=std::any_of(s.tracks.begin(),s.tracks.end(),[](const Track& tr){return tr.solo;});
  oldState.reset();newState.reset();
  for(size_t tr=0;tr<s.tracks.size();++tr){
   std::array<float,256> oldL{},oldR{},newL{},newR{};
   renderTrackBlock(s,tr,0,256,oldL.data(),oldR.data(),oldState,true,true);
   renderTrackBlock(s,tr,0,256,newL.data(),newR.data(),newState,true,true,anySolo);
   for(size_t n=0;n<256;++n){near(oldL[n],newL[n]);near(oldR[n],newR[n]);}
  }
 }
}
static void masteringFinite(){
 MasterSettings m;m.gain=1.7f;m.limiterEnabled=true;m.ceilingDb=-1.f;
 std::array<float,6> l{0.f,.2f,2.f,-3.f,INFINITY,NAN},r{.4f,-.3f,-2.1f,2.4f,NAN,INFINITY};
 processMasterBlock(m,int(l.size()),l.data(),r.data());
 const float ceiling=std::pow(10.f,m.ceilingDb/20.f);
 for(size_t i=0;i<l.size();++i){
  check(std::isfinite(l[i])&&std::isfinite(r[i]),"Master leaked invalid audio");
  check(std::max(std::abs(l[i]),std::abs(r[i]))<=ceiling+1.e-5f,"Master ceiling exceeded");
 }
}
int main(){eightBusRouting();soloCacheEquivalent();masteringFinite();std::cout<<"PASS: eight-bus routing, no self-send doubling, solo-cache audio parity, master output validation\n";}
