#include "TimeWarp.h"
#include "AudioCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
using namespace veysona;
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL: "<<#x<<" at line "<<__LINE__<<"\n";return 1;}}while(false)
static Clip sine(uint32_t sr,int frames,double hz){
 Clip c;c.name="Test Sine";c.sampleRate=sr;c.stereo.resize(size_t(frames)*2);
 for(int i=0;i<frames;++i){const auto value=.48f*std::sin(float(2*kPi*hz*i/sr));c.stereo[2*i]=value;c.stereo[2*i+1]=value*.6f;}
 buildWaveformPeaks(c);return c;
}
static double hzEstimate(const Clip& c){
 // Count positive zero crossings in middle 50%, avoiding transient grain boundaries.
 const size_t n=c.stereo.size()/2,a=n/4,b=3*n/4;
 int crosses=0;
 for(size_t i=a+1;i<b;++i)if(c.stereo[2*(i-1)]<=0&&c.stereo[2*i]>0)++crosses;
 return crosses*double(c.sampleRate)/double(b-a);
}
static bool finite(const Clip& c){
 return std::all_of(c.stereo.begin(),c.stereo.end(),[](float x){return std::isfinite(x)&&std::abs(x)<10.f;});
}
int main(){
 for(double speed:{.25,.5,.75,1.25,2.,4.}){
  auto c=sine(48000,48000,440);warp::stretchClip(c,speed);
  CHECK(c.stereo.size()/2==size_t(std::llround(48000/speed)));
  CHECK(finite(c));CHECK(!c.waveformPeaks.empty());
  const auto freq=hzEstimate(c);
  CHECK(std::abs(freq-440.)<55.); // WSOLA is an approximate rather than phase-vocoder stretcher
 }
 auto pitched=sine(48000,48000,220.);
 warp::pitchShiftClip(pitched,12);
 CHECK(pitched.stereo.size()/2==48000&&finite(pitched));
 CHECK(std::abs(hzEstimate(pitched)-440.)<55.);
 auto down=sine(48000,48000,440.);
 warp::pitchShiftClip(down,-12);
 CHECK(down.stereo.size()/2==48000&&finite(down));
 CHECK(std::abs(hzEstimate(down)-220.)<45.);
 auto fit=sine(48000,48000,120.);
 warp::fitClipToBeats(fit,4.,120.);
 CHECK(fit.stereo.size()/2==96000&&finite(fit));
 auto piece=sine(48000,48000,320.);
 warp::warpClipToMarkers(piece,{{0.,0.},{.5,.6},{1.,1.2}});
 CHECK(piece.stereo.size()/2==57600&&finite(piece));
 for(double bad:{std::numeric_limits<double>::quiet_NaN(),0.,5.,-1.}){
  bool failed=false;try{auto c=sine(48000,48000,100.);warp::stretchClip(c,bad);}catch(const std::exception&){failed=true;}CHECK(failed);
 }
 bool rejected=false;try{warp::warpClipToMarkers(piece,{{0,0},{.6,.4},{.3,1.}});}catch(const std::exception&){rejected=true;}CHECK(rejected);
 auto one=sine(48000,48000,220.),two=sine(48000,48000,330.);
 one.startBeat=0;two.startBeat=2.;
 const auto overlap=warp::crossfadeAdjacent(one,two,120.,.2);
 CHECK(std::abs(overlap-.2)<1e-5&&one.equalPowerFades&&two.equalPowerFades);
 CHECK(std::abs(one.fadeOutSeconds-.2f)<1e-4&&std::abs(two.fadeInSeconds-.2f)<1e-4);
 CHECK(std::abs(two.startBeat-1.6)<1e-4);
 // Detect transient proposals without having to force an unreliable marker.
 auto onset=sine(48000,48000,110.);
 for(size_t f=10000;f<10080;++f){onset.stereo[2*f]+=.8f;onset.stereo[2*f+1]+=.8f;}
 auto markers=warp::suggestBeatMarkers(onset,120.,.5);
 CHECK(markers.size()>=2&&markers.front().sourceSeconds==0&&
       std::abs(markers.back().sourceSeconds-1.)<.0001);
 CHECK(markers.back().targetSeconds==1.);
 // On-disk original samples remain unmodified: audio edits clear sourcePath.
 auto path=sine(48000,12000,200.);path.sourcePath="C:/original.wav";
 warp::stretchClip(path,.5);CHECK(path.sourcePath.empty());
 std::cout<<"PASS: WSOLA speed / pitch shift / tempo fit / markers / crossfade / bounds / transients\n";
 return 0;
}
