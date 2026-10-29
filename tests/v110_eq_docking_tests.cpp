#include "AudioCore.h"
#include "UiGeometry.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#define CHECK(e) do { if(!(e)){std::cerr<<"CHECK FAILED: "<<#e<<" on line "<<__LINE__<<'\n';std::abort();} } while(false)
int main(){
 using namespace veysona;
 using namespace veysona::ui;
 // Every viewport must have enough room for its usable controls.
 for(int width:{1200,1280,1366,1600,1920,2560})for(int height:{720,900,1080,1440}){
  for(int bw:{0,250,294,450,560,800})for(int eh:{150,310,385,600,950}){
   auto w=makeWorkspaceLayout(width,height,bw!=0,true,bw,eh);
   CHECK(w.rightEdge>=900);
   CHECK(w.editorHeight>=310);
   CHECK(w.arrangementBottom-w.arrangementTop>=230);
   CHECK(w.footerTop==height-27);
   auto cols=makeDockColumns(w.rightEdge,.42f,.22f);
   CHECK(cols.editor>=310);
   CHECK(cols.instrument>=185);
   CHECK(cols.equalizer>=280);
   CHECK(cols.editor+cols.instrument+cols.equalizer==w.rightEdge);
  }
 }
 // Gain mapping must be monotonically rising and round-trip correctly.
 float previous=-1.f;
 for(int i=0;i<=1000;++i){
  float position=float(i)/1000.f;
  auto gain=trackGainFromKnob(position);
  CHECK(std::isfinite(gain));CHECK(gain>=previous);
  CHECK(std::abs(trackGainToKnob(gain)-position)<.003f);
  previous=gain;
 }
 CHECK(std::abs(trackGainFromKnob(.8f)-1.f)<1e-4f);
 CHECK(std::abs(trackGainFromKnob(1.f)-1.5f)<1e-4f);
 CHECK(trackGainFromKnob(0.f)==0.f);
 EqSettings eq;
 eq.bands[0].gainDb=10.f;
 CHECK(eqMagnitudeDb(eq,40.,48000)>7.);
 eq.bands[0].enabled=false;
 CHECK(std::abs(eqMagnitudeDb(eq,40.,48000))<.0001);
 eq.bands[0].enabled=true;
 eq.enabled=false;
 CHECK(std::abs(eqMagnitudeDb(eq,40.,48000))<.0001);
 eq.enabled=true;
 eq.bands[2].mode=EqMode::Bell;
 eq.bands[2].frequency=2800;
 eq.bands[2].q=2.f;
 eq.bands[2].gainDb=-11.f;
 CHECK(eqMagnitudeDb(eq,2800.,48000)<-7.);
 for(auto rate:{22050u,44100u,48000u,96000u}){
  auto coeff=makeEqCoefficients(eq.bands[2],rate);
  CHECK(std::isfinite(coeff.b0) && std::isfinite(coeff.a1));
 }
 // Dragging/sweeping in playback should not produce NaNs or extreme peaks.
 Session session;session.sampleRate=48000;session.tracks.resize(1);
 EngineState engine;engine.prepare(1,session.sampleRate);
 std::array<float,512> left{},right{};
 for(int k=0;k<90;++k){
  auto& band=session.tracks[0].eq.bands[2];
  band.gainDb=-12.f+24.f*k/89.f;
  band.frequency=200.f+7800.f*k/89.f;
  for(int i=0;i<512;++i){left[i]=right[i]=.12f*std::sin(float((k*512+i)*6.283185307179586*1200./48000.));}
  processTrackEffects(session,0,512,left.data(),right.data(),engine,int64_t(k)*512);
  for(int i=0;i<512;++i){CHECK(std::isfinite(left[i]));CHECK(std::abs(left[i])<2.f);}
 }
 std::cout<<"PASS: v1.10 EQ bypass, automation smoothing, channel dial curve, dock resize bounds\n";
}
