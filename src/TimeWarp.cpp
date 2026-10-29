#include "TimeWarp.h"
#include "ProductionAnalysis.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace veysona::warp {
namespace {
constexpr size_t kMaxFrames=15'000'000;
void checkAudio(const std::vector<float>& stereo,uint32_t sr){
 if(sr<8000||sr>384000||stereo.empty()||stereo.size()%2||stereo.size()/2>kMaxFrames)
  throw std::invalid_argument("Invalid stereo clip for offline time warp (max 15 million frames)");
}
size_t frameCount(double seconds,uint32_t sr){
 if(!std::isfinite(seconds)||seconds<=0||seconds*sr>kMaxFrames)
  throw std::invalid_argument("Time warp would exceed audio length limits");
 return std::max(size_t(1),size_t(std::llround(seconds*sr)));
}
// High-quality, stereo-linked windowed-sinc pitch resampling.
// Linear interpolation used in v1.18 had no anti-aliasing when raising pitch,
// so frequencies above the new Nyquist limit folded into the audible band.
// Blackman-windowed sinc, with cutoff adjusted for the decimation factor.
// This runs OFFLINE, never on the real-time audio callback.
std::vector<float> resamplePitchBandlimitedImpl(const std::vector<float>& in,double factor){
 if(in.empty() || (in.size()&1u) || !std::isfinite(factor) || factor<=0.)
  throw std::invalid_argument("Invalid pitch resampling input or ratio");
 const size_t n=in.size()/2;
 if(double(n)/factor>double(kMaxFrames)+.5)
  throw std::length_error("Pitch shifting would create too much audio");
 const size_t m=std::max(size_t(1),size_t(std::llround(double(n)/factor)));
 if(m>kMaxFrames)throw std::length_error("Pitch shifting would create too much audio");
 if(factor==1.)return in;
 constexpr int radius=36; // 73-tap Blackman FIR: useful attenuation above Nyquist.
 const double cutoff=std::min(1.,1./factor);
 std::vector<float> out(m*2);
 for(size_t i=0;i<m;++i){
  const double source=std::min(double(n-1),double(i)*factor);
  const int64_t mid=int64_t(std::floor(source));
  double total=0.,left=0.,right=0.;
  for(int tap=-radius;tap<=radius;++tap){
   const int64_t pos=mid+tap;
   const double dx=double(pos)-source;
   const double u=std::abs(dx)/double(radius+1);
   if(u>=1.)continue;
   const double window=.42+.5*std::cos(kPi*u)+.08*std::cos(2.*kPi*u);
   const double x=kPi*cutoff*dx;
   const double sinc=std::abs(x)<1e-10?1.:std::sin(x)/x;
   const double weight=cutoff*sinc*window;
   // Repeat boundary samples rather than zero-padding: no fade at clip ends.
   const size_t index=size_t(std::clamp(pos,int64_t(0),int64_t(n-1)));
   left+=double(in[2*index])*weight;
   right+=double(in[2*index+1])*weight;
   total+=weight;
  }
  const double norm=std::abs(total)>1e-10?1./total:1.;
  out[2*i]=float(left*norm);
  out[2*i+1]=float(right*norm);
 }
 return out;
}
void sanitize(std::vector<float>& s){
 for(auto& v:s)if(!std::isfinite(v))v=0.f;
}
}
std::vector<float> resamplePitchBandlimited(const std::vector<float>& in,double factor){
 return resamplePitchBandlimitedImpl(in,factor);
}
std::vector<float> stretchWsola(const std::vector<float>& input,uint32_t sr,double speed,Options opts){
 checkAudio(input,sr);
 if(!std::isfinite(speed)||speed<.25||speed>4.)
  throw std::invalid_argument("Time stretch speed must be between 0.25x and 4x");
 if(!std::isfinite(opts.searchMs)||opts.searchMs<0||opts.searchMs>40)
  throw std::invalid_argument("Invalid stretch search window");
 const size_t sourceFrames=input.size()/2;
 const size_t outputFrames=std::max(size_t(1),size_t(std::llround(sourceFrames/speed)));
 if(outputFrames>kMaxFrames)throw std::length_error("Time stretch would exceed maximum frames");
 if(std::abs(speed-1.)<1e-9)return input;
 if(sourceFrames<512||outputFrames<512)return resamplePitchBandlimited(input,double(sourceFrames)/outputFrames); // short transient fallback
 const bool fidelity=opts.quality==Quality::HighFidelity;
 // The higher-fidelity offline mode uses 75% overlap and a longer analysis
 // window, reducing periodic amplitude modulation on sustained material.
 const size_t grain=std::min<size_t>(sourceFrames/2,
      std::clamp(size_t(sr/(fidelity?17:25)),size_t(512),size_t(fidelity?8192:4096)));
 const size_t hop=std::max(size_t(64),grain/(fidelity?4:2));
 // Search candidate offsets are evaluated with stereo-linked normalized cross correlation.
 // Both channels use the SAME analysis position to avoid stereo drift.
 const int radius=int(std::round(sr*opts.searchMs/1000.));
 const int step=std::max(2,int(sr/(fidelity?24000:12000)));
 std::vector<float> output(outputFrames*2,0.f),sum(outputFrames,0.f);
 size_t previousStart=0;
 for(size_t dstStart=0;dstStart<outputFrames;dstStart+=hop){
  const auto expected=int64_t(std::llround(double(dstStart)*speed));
  const auto maxStart=int64_t(sourceFrames-1);
  size_t bestStart=size_t(std::clamp(expected,int64_t(0),maxStart));
  if(dstStart>0&&radius>0){
   double bestScore=-2.0;
   const int64_t lo=std::max(int64_t(previousStart+1),std::max(int64_t(0),expected-radius));
   const int64_t hi=std::min(maxStart,std::max(lo,expected+radius));
   // Evaluate a bounded number of candidates and sample points.
   const int candidateStride=std::max(step,int((hi-lo)/(fidelity?100:50))+1);
   const size_t compareFrames=std::min({grain/2,outputFrames-dstStart,sourceFrames/2});
   for(int64_t candidate=lo;candidate<=hi;candidate+=candidateStride){
    double dot=0,aa=1e-14,bb=1e-14;
    for(size_t k=0;k<compareFrames;k+=fidelity?4:8){
     const size_t dst=dstStart+k,src=size_t(candidate)+k;
     if(src>=sourceFrames||sum[dst]<.02f)continue;
     // Match the stereo image, NOT an L+R mono fold-down. An out-of-phase
     // stereo recording can cancel to zero in mono; the old matcher then
     // chose arbitrary grain positions, causing audible periodic artifacts.
     const double aL=output[2*dst]/sum[dst];
     const double aR=output[2*dst+1]/sum[dst];
     const double bL=input[2*src];
     const double bR=input[2*src+1];
     dot+=aL*bL+aR*bR;
     aa+=aL*aL+aR*aR;
     bb+=bL*bL+bR*bR;
    }
    const double similarity=dot/std::sqrt(aa*bb);
    // Percussive mode prioritizes timing, smooth mode uses stronger phase alignment.
    const double deviation=std::abs(double(candidate-expected))/std::max(1,radius);
    const double score=similarity-(opts.quality==Quality::Percussive?.22:fidelity?.018:.035)*deviation;
    if(score>bestScore){bestScore=score;bestStart=size_t(candidate);}
   }
  }
  previousStart=bestStart;
  const size_t usable=std::min({grain,sourceFrames-bestStart,outputFrames-dstStart});
  for(size_t k=0;k<usable;++k){
   // Raised cosine window, with adjacent overlapping grains normalized to unity.
   // First and last window edges are handled below for nonzero output samples.
   const double phase=(double(k)+.5)/grain;
   const float window=float(.5-.5*std::cos(2.*kPi*phase));
   const size_t src=bestStart+k,dst=dstStart+k;
   sum[dst]+=window;
   output[2*dst]+=input[2*src]*window;
   output[2*dst+1]+=input[2*src+1]*window;
  }
 }
 for(size_t frame=0;frame<outputFrames;++frame){
  if(sum[frame]>1e-7f){output[2*frame]/=sum[frame];output[2*frame+1]/=sum[frame];}
  else { // Tiny ending gaps, if any, are filled by nearest valid original sample.
   const size_t src=std::min(sourceFrames-1,size_t(std::floor(frame*speed)));
   output[2*frame]=input[2*src];output[2*frame+1]=input[2*src+1];
  }
 }
 sanitize(output);
 return output;
}
void stretchClip(Clip& clip,double speed,Options options){
 auto result=stretchWsola(clip.stereo,clip.sampleRate,speed,options);
 clip.stereo.swap(result);clip.sourcePath.clear();buildWaveformPeaks(clip);
}
void fitClipToBeats(Clip& clip,double beats,double bpm,Options options){
 if(!std::isfinite(beats)||beats<=0||beats>256||!std::isfinite(bpm)||bpm<20||bpm>400)
  throw std::invalid_argument("Invalid target beats or project tempo");
 checkAudio(clip.stereo,clip.sampleRate);
 const size_t target=frameCount(beats*60./bpm,clip.sampleRate);
 const double speed=double(clip.stereo.size()/2)/target;
 if(speed<.25||speed>4.)throw std::invalid_argument("Tempo fit requires speed between 0.25x and 4x");
 auto audio=stretchWsola(clip.stereo,clip.sampleRate,speed,options);
 audio.resize(target*2,0.f);clip.stereo.swap(audio);clip.sourcePath.clear();buildWaveformPeaks(clip);
}
void pitchShiftClip(Clip& clip,double semitones,Options options){
 if(!std::isfinite(semitones)||semitones<-12||semitones>12)
  throw std::invalid_argument("Pitch change must be -12 to +12 semitones");
 checkAudio(clip.stereo,clip.sampleRate);
 if(std::abs(semitones)<1e-9)return;
 const size_t original=clip.stereo.size()/2;
 const double factor=std::pow(2.,semitones/12.);
 auto resampled=resamplePitchBandlimited(clip.stereo,factor);
 const double speed=double(resampled.size()/2)/original;
 auto output=stretchWsola(resampled,clip.sampleRate,speed,options);
 output.resize(original*2,0.f);
 clip.stereo.swap(output);clip.sourcePath.clear();buildWaveformPeaks(clip);
}
void warpClipToMarkers(Clip& clip,const std::vector<Marker>& markers,Options options){
 checkAudio(clip.stereo,clip.sampleRate);
 if(markers.size()<2||markers.size()>65)throw std::invalid_argument("Warp expects 2 to 65 markers");
 const double originalDuration=double(clip.stereo.size()/2)/clip.sampleRate;
 if(std::abs(markers.front().sourceSeconds)>1e-6||std::abs(markers.front().targetSeconds)>1e-6||
    std::abs(markers.back().sourceSeconds-originalDuration)>1.5/clip.sampleRate)
  throw std::invalid_argument("Warp markers must include exact start and finish of source");
 const size_t outFrames=frameCount(markers.back().targetSeconds,clip.sampleRate);
 size_t consumed=0;
 std::vector<float> output(outFrames*2,0.f);
 for(size_t i=1;i<markers.size();++i){
  const auto& a=markers[i-1];const auto& b=markers[i];
  if(!std::isfinite(a.sourceSeconds)||!std::isfinite(b.sourceSeconds)||
     !std::isfinite(a.targetSeconds)||!std::isfinite(b.targetSeconds)||
     a.sourceSeconds<0||a.targetSeconds<0||b.sourceSeconds<=a.sourceSeconds||b.targetSeconds<=a.targetSeconds)
   throw std::invalid_argument("Warp marker positions must increase strictly");
  const size_t sa=std::min(clip.stereo.size()/2,size_t(std::llround(a.sourceSeconds*clip.sampleRate)));
  const size_t sb=std::min(clip.stereo.size()/2,size_t(std::llround(b.sourceSeconds*clip.sampleRate)));
  const size_t da=std::min(outFrames,size_t(std::llround(a.targetSeconds*clip.sampleRate)));
  const size_t db=std::min(outFrames,size_t(std::llround(b.targetSeconds*clip.sampleRate)));
  if(sa>=sb||da>=db||da<consumed)throw std::invalid_argument("Warp marker segment shorter than one audio frame");
  const double speed=double(sb-sa)/(db-da);
  if(speed<.25||speed>4.)throw std::invalid_argument("Warp segment exceeds supported 0.25x–4x speed");
  std::vector<float> part(clip.stereo.begin()+std::ptrdiff_t(sa*2),clip.stereo.begin()+std::ptrdiff_t(sb*2));
  auto changed=stretchWsola(part,clip.sampleRate,speed,options);
  changed.resize((db-da)*2,0.f);
  std::copy(changed.begin(),changed.end(),output.begin()+std::ptrdiff_t(da*2));
  consumed=db;
 }
 // Smooth tiny segment seams without moving anchors or clip length.
 // The short, cross-channel-identical transition avoids broadband clicks.
 const size_t blendFrames=std::max(size_t(1),size_t(clip.sampleRate/400)); // 2.5 ms max
 for(size_t j=1;j+1<markers.size();++j){
  const auto center=std::min(outFrames-1,size_t(std::llround(markers[j].targetSeconds*clip.sampleRate)));
  const size_t lo=center>blendFrames?center-blendFrames:0;
  const size_t hi=std::min(outFrames,center+blendFrames);
  if(center==0||center>=outFrames||hi-lo<4)continue;
  for(size_t i=lo;i<hi;++i){
   const double interp=double(i-lo)/double(hi-lo);
   const size_t l=std::max(size_t(0),center-1),r=center;
   for(int c=0;c<2;++c){
    const float smoothed=float(output[2*l+c]*(1.-interp)+output[2*r+c]*interp);
    const double strength=std::clamp(1.-std::abs(double(i-center))/blendFrames,0.,1.);
    output[2*i+c]=float(output[2*i+c]*(1.-.65*strength)+smoothed*(.65*strength));
   }
  }
 }
 clip.stereo.swap(output);clip.sourcePath.clear();buildWaveformPeaks(clip);
}
std::vector<Marker> suggestBeatMarkers(const Clip& clip,double bpm,double subdivisionBeats,size_t limit){
 checkAudio(clip.stereo,clip.sampleRate);
 if(!std::isfinite(bpm)||bpm<20||bpm>400||!std::isfinite(subdivisionBeats)||subdivisionBeats<.125||subdivisionBeats>4||limit<2||limit>65)
  throw std::invalid_argument("Invalid transient warp grid");
 const double duration=double(clip.stereo.size()/2)/clip.sampleRate;
 std::vector<Marker> markers{{0,0}};
 const double grid=subdivisionBeats*60./bpm;
 for(double onset:production::detectTransients(clip.stereo,clip.sampleRate,1.5,.095)){
  const double snapped=std::round(onset/grid)*grid;
  if(snapped<=0||snapped>=duration-.02||onset<=markers.back().sourceSeconds+.08||
     snapped<=markers.back().targetSeconds+.08||markers.size()>=limit-1)continue;
  if(std::abs(snapped-onset)>.12)continue; // never drag beats too far
  const double srcDelta=onset-markers.back().sourceSeconds;
  const double dstDelta=snapped-markers.back().targetSeconds;
  if(srcDelta/dstDelta<.3||srcDelta/dstDelta>3.5)continue;
  if((duration-onset)/(duration-snapped)<.3||(duration-onset)/(duration-snapped)>3.5)continue;
  markers.push_back({onset,snapped});
 }
 markers.push_back({duration,duration});
 return markers;
}
double crossfadeAdjacent(Clip& left,Clip& right,double bpm,double requested){
 if(&left==&right||!std::isfinite(bpm)||bpm<20||bpm>400||
    !std::isfinite(requested)||requested<=0||requested>2||
    left.stereo.empty()||right.stereo.empty()||!left.sampleRate||!right.sampleRate)
  throw std::invalid_argument("Invalid crossfade clips or duration");
 const double firstLength=double(left.stereo.size()/2)/left.sampleRate;
 const double secondLength=double(right.stereo.size()/2)/right.sampleRate;
 const double fade=std::min({requested,firstLength*.5,secondLength*.5});
 const double leftEnd=left.startBeat+firstLength*bpm/60.;
 // Ensure the following clip begins before the previous one ends.
 right.startBeat=leftEnd-fade*bpm/60.;
 left.fadeOutSeconds=float(fade);right.fadeInSeconds=float(fade);
 left.equalPowerFades=true;right.equalPowerFades=true;
 return fade;
}
}
