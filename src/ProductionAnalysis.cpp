#include "ProductionAnalysis.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace veysona::production {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr double tiny=1e-12;
double db(double v){return 20.*std::log10(std::max(v,tiny));}
void validate(const std::vector<float>& stereo,uint32_t rate){
 if(rate<8000||rate>384000||stereo.size()%2)throw std::invalid_argument("Expected stereo audio and sample rate 8-384 kHz");
 for(float x:stereo)if(!std::isfinite(x))throw std::invalid_argument("Audio contains NaN or infinity");
}
struct Biquad {double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
 double process(double v){double out=b0*v+z1;z1=b1*v-a1*out+z2;z2=b2*v-a2*out;return out;}
};
Biquad highPass(double hz,double q,double sr){
 const double w=2*pi*hz/sr,alpha=std::sin(w)/(2*q),c=std::cos(w),a0=1+alpha;
 return {((1+c)*.5)/a0,-(1+c)/a0,((1+c)*.5)/a0,-2*c/a0,(1-alpha)/a0};
}
Biquad shelf(double hz,double gainDb,double sr){
 const double a=std::pow(10.,gainDb/40),w=2*pi*hz/sr,c=std::cos(w),s=std::sin(w);
 const double alpha=s/std::sqrt(2.);const double t=2*std::sqrt(a)*alpha;
 const double a0=(a+1)-(a-1)*c+t;
 return {a*((a+1)+(a-1)*c+t)/a0,-2*a*((a-1)+(a+1)*c)/a0,a*((a+1)+(a-1)*c-t)/a0,
         2*((a-1)-(a+1)*c)/a0,((a+1)-(a-1)*c-t)/a0};
}
std::vector<double> onsetEnvelope(const std::vector<float>& audio,uint32_t rate){
 const size_t hop=std::max<size_t>(1,rate/100); // ~10 ms
 const size_t frames=audio.size()/2;
 std::vector<double> novelty((frames+hop-1)/hop,0.);
 double last=0;
 for(size_t k=0;k<novelty.size();++k){
  double power=0;size_t count=0;
  for(size_t i=k*hop;i<std::min(frames,(k+1)*hop);++i){
   const double l=audio[2*i],r=audio[2*i+1];power+=(l*l+r*r)*.5;++count;
  }
  double magnitude=std::sqrt(power/std::max<size_t>(1,count));
  novelty[k]=std::max(0.,magnitude-last);last=magnitude;
 }
 // Smooth a single hop to resist odd clicks.
 auto result=novelty;
 for(size_t k=1;k+1<novelty.size();++k)result[k]=.2*novelty[k-1]+.6*novelty[k]+.2*novelty[k+1];
 return result;
}
// Sample the source with normalized band-limited sinc so DC gain remains 1 at edges.
double sincSample(const std::vector<float>& audio,size_t frames,int channel,double pos,double ratio,int taps){
 if(frames==0)return 0;
 const double cutoff=std::min(1.,ratio); // rate ratio = output / input
 const int half=taps/2;
 const int64_t center=int64_t(std::floor(pos));
 double accum=0,weight=0;
 for(int t=-half+1;t<=half;++t){
  int64_t src=center+t;if(src<0||uint64_t(src)>=frames)continue;
  const double x=pos-src;
  const double ap=std::abs(x)/half;
  if(ap>=1.)continue;
  const double window=.5+.5*std::cos(pi*x/half);
  const double z=pi*x*cutoff;
  const double w=cutoff*(std::abs(z)<1e-10?1.:std::sin(z)/z)*window;
  accum+=audio[2*size_t(src)+size_t(channel)]*w;weight+=w;
 }
 return std::abs(weight)<tiny?0.:accum/weight;
}
}
std::vector<double> detectTransients(const std::vector<float>& stereo,uint32_t sampleRate,
                                     double sensitivity,double minSeparationSeconds){
 validate(stereo,sampleRate);
 if(sensitivity<.1||sensitivity>10||minSeparationSeconds<0||minSeparationSeconds>10)
  throw std::invalid_argument("Invalid transient detector parameters");
 auto energy=onsetEnvelope(stereo,sampleRate);
 if(energy.size()<4)return {};
 std::vector<double> sorted=energy;
 std::sort(sorted.begin(),sorted.end());
 const auto noise=sorted[size_t((sorted.size()-1)*.75)];
 const auto strongest=sorted.back();
 if(strongest<1e-6)return {};
 const auto threshold=std::max(strongest*.085*sensitivity,noise*1.4*sensitivity);
 const size_t separation=std::max<size_t>(1,size_t(std::ceil(minSeparationSeconds*100)));
 std::vector<double> result;
 size_t last=0;
 for(size_t i=1;i+1<energy.size();++i){
  if(energy[i]<threshold||energy[i]<energy[i-1]||energy[i]<energy[i+1])continue;
  if(!result.empty()&&i-last<separation){
   if(energy[i]>energy[last]){result.back()=double(i)*.01;last=i;}
   continue;
  }
  result.push_back(double(i)*.01);last=i;
 }
 return result;
}
std::vector<float> resampleSinc(const std::vector<float>& stereo,uint32_t inputRate,
                                uint32_t outputRate,int taps){
 validate(stereo,inputRate);
 if(outputRate<8000||outputRate>384000||taps<8||taps>256||taps%2)
  throw std::invalid_argument("Invalid resampling configuration");
 if(inputRate==outputRate)return stereo;
 const size_t frames=stereo.size()/2;
 if(frames>50000000)throw std::length_error("Resample input too long");
 const long double requested=static_cast<long double>(frames)*outputRate/inputRate;
 if(requested>250000000)throw std::length_error("Resample output too large");
 const size_t target=static_cast<size_t>(std::llround(requested));
 std::vector<float> out(target*2,0);
 const double ratio=double(outputRate)/inputRate;
 for(size_t i=0;i<target;++i){
  const double pos=double(i)/ratio;
  out[i*2]=float(sincSample(stereo,frames,0,pos,ratio,taps));
  out[i*2+1]=float(sincSample(stereo,frames,1,pos,ratio,taps));
 }
 return out;
}
double estimatedTruePeakDb(const std::vector<float>& stereo,uint32_t sampleRate,int oversample){
 validate(stereo,sampleRate);
 if(oversample!=2&&oversample!=4&&oversample!=8)throw std::invalid_argument("Oversampling must be 2, 4, or 8");
 const size_t frames=stereo.size()/2;
 if(!frames)return -120.;
 // The interpolation phases never change. Cache windowed-sinc taps rather
 // than recomputing sin()/cos() for every sample/channel (very expensive for
 // a 2-minute track, particularly with instrumented builds).
 constexpr int taps=24,half=taps/2;
 std::array<std::array<double,taps>,8> kernels{};
 for(int phase=1;phase<oversample;++phase){
  const double fraction=double(phase)/oversample;
  for(int t=0;t<taps;++t){
   const double x=fraction-(-half+1+t);
   const double window=.5+.5*std::cos(pi*x/half);
   const double z=pi*x;
   kernels[size_t(phase)][size_t(t)]=(std::abs(x)>=half)?0.:
     (std::abs(z)<1e-10?1.:std::sin(z)/z)*window;
  }
 }
 double peak=0;
 for(float sample:stereo)peak=std::max(peak,std::abs(double(sample)));
 for(size_t i=0;i+1<frames;++i){
  for(int phase=1;phase<oversample;++phase){
   double sumL=0,sumR=0,weights=0;
   for(int t=0;t<taps;++t){
    const int64_t index=int64_t(i)+int64_t(t-half+1);
    if(index<0||uint64_t(index)>=frames)continue;
    const auto w=kernels[size_t(phase)][size_t(t)];
    const auto n=size_t(index)*2;
    sumL+=double(stereo[n])*w;sumR+=double(stereo[n+1])*w;weights+=w;
   }
   if(std::abs(weights)>tiny)peak=std::max({peak,std::abs(sumL/weights),std::abs(sumR/weights)});
  }
 }
 return db(peak);
}
double integratedLoudnessApprox(const std::vector<float>& stereo,uint32_t sampleRate){
 validate(stereo,sampleRate);
 const size_t frames=stereo.size()/2;
 if(frames==0)return -120.;
 auto lpL=highPass(38.,.5,sampleRate),lpR=lpL;
 auto shL=shelf(1681.974,4.,sampleRate),shR=shL;
 std::vector<double> power(frames);
 for(size_t i=0;i<frames;++i){
  auto l=lpL.process(shL.process(stereo[i*2]));
  auto r=lpR.process(shR.process(stereo[i*2+1]));
  power[i]=l*l+r*r;
 }
 const size_t block=std::max<size_t>(1,size_t(.4*sampleRate));
 const size_t hop=std::max<size_t>(1,size_t(.1*sampleRate));
 std::vector<double> energies;
 double sum=std::accumulate(power.begin(),power.begin()+std::min(block,frames),0.);
 if(frames<block){energies.push_back(sum/frames);}
 else {
  energies.push_back(sum/block);
  for(size_t offset=hop;offset+block<=frames;offset+=hop){
   for(size_t k=offset-hop;k<offset;++k)sum-=power[k];
   for(size_t k=offset-hop+block;k<offset+block;++k)sum+=power[k];
   energies.push_back(std::max(0.,sum/double(block)));
  }
 }
 const auto lufs=[](double energy){return -0.691+10.*std::log10(std::max(energy,tiny));};
 double gatedSum=0;size_t gatedCount=0;
 for(auto e:energies)if(lufs(e)>-70){gatedSum+=e;++gatedCount;}
 if(!gatedCount)return -120.;
 const double relativeGate=lufs(gatedSum/gatedCount)-10.;
 double finalSum=0;size_t finalCount=0;
 for(auto e:energies)if(lufs(e)>-70&&lufs(e)>relativeGate){finalSum+=e;++finalCount;}
 return finalCount?lufs(finalSum/finalCount):-120.;
}
LoopAnalysis analyseLoop(const std::vector<float>& stereo,uint32_t sampleRate,double minBpm,double maxBpm){
 validate(stereo,sampleRate);
 if(minBpm<20||maxBpm>400||minBpm>=maxBpm)throw std::invalid_argument("Invalid BPM range");
 LoopAnalysis result;
 if(stereo.empty())return result;
 double sumSq=0,peak=0,leftPower=0,rightPower=0,crossPower=0;
 for(size_t i=0;i<stereo.size();i+=2){
  const double l=stereo[i],r=stereo[i+1];
  leftPower+=l*l;rightPower+=r*r;crossPower+=l*r;
  sumSq+=l*l+r*r;peak=std::max({peak,std::abs(l),std::abs(r)});
 }
 if(leftPower>tiny&&rightPower>tiny){
  result.stereoCorrelation=std::clamp(crossPower/std::sqrt(leftPower*rightPower),-1.,1.);
  result.channelBalanceDb=std::clamp(10.*std::log10(leftPower/rightPower),-120.,120.);
 }else if(leftPower>tiny||rightPower>tiny){
  result.channelBalanceDb=leftPower>tiny?120.:-120.;
 }
 result.peakDbfs=db(peak);result.rmsDbfs=db(std::sqrt(sumSq/stereo.size()));
 result.approximateLufs=integratedLoudnessApprox(stereo,sampleRate);
 result.estimatedTruePeakDbfs=estimatedTruePeakDb(stereo,sampleRate,4);
 result.transientsSeconds=detectTransients(stereo,sampleRate,1.);
 // Spectral centroid on first 4096 frames; inexpensive O(N^2) DFT only at 128 evenly spaced bins.
 const size_t frames=stereo.size()/2,N=std::min<size_t>(1024,frames);
 double weighted=0,mass=0;
 if(N>=128){
  for(size_t k=1;k<N/2;k+=4){
   std::complex<double> z{0,0};
   for(size_t t=0;t<N;++t){
    const auto a=2*pi*k*t/N;
    const auto mono=(double(stereo[2*t])+stereo[2*t+1])*.5;
    const auto window=.5-.5*std::cos(2*pi*t/(N-1));
    z+=std::complex<double>(std::cos(a),-std::sin(a))*(mono*window);
   }
   const auto mag=std::abs(z);weighted+=mag*k*sampleRate/N;mass+=mag;
  }
  if(mass>tiny)result.spectralCentroidHz=weighted/mass;
 }
 auto env=onsetEnvelope(stereo,sampleRate);
 const auto duration=double(stereo.size()/2)/sampleRate;
 if(duration<1.5||env.size()<150)return result;
 const double total=std::accumulate(env.begin(),env.end(),0.);
 if(total<1e-5)return result;
 const int minLag=std::max(1,int(std::floor(6000./maxBpm)));
 const int maxLag=std::min(int(env.size())-1,int(std::ceil(6000./minBpm)));
 // Penalise octave aliases gently; confidence compares lag similarity against envelope energy.
 double best=0;int bestLag=0;
 double variance=0;for(auto v:env)variance+=v*v;
 for(int lag=minLag;lag<=maxLag;++lag){
  double dot=0;for(size_t t=size_t(lag);t<env.size();++t)dot+=env[t]*env[t-size_t(lag)];
  double score=dot/std::max(variance,tiny);
  if(score>best){best=score;bestLag=lag;}
 }
 if(bestLag>0&&best>.035){result.estimatedBpm=6000./bestLag;result.confidence=std::clamp(best,0.,1.);}
 return result;
}
MasteringReport normalizeLoudnessOffline(std::vector<float>& stereo,uint32_t rate,double targetLufs,double ceilingDb){
 validate(stereo,rate);
 if(targetLufs<-40||targetLufs>-5||ceilingDb<-12||ceilingDb>0)
  throw std::invalid_argument("Target loudness or ceiling outside supported range");
 MasteringReport r;
 r.inputLufs=integratedLoudnessApprox(stereo,rate);
 r.inputTruePeak=estimatedTruePeakDb(stereo,rate);
 if(stereo.empty()||r.inputLufs<=-110){r.outputLufs=r.inputLufs;r.outputTruePeak=r.inputTruePeak;return r;}
 double gainDb=std::clamp(targetLufs-r.inputLufs,-36.,36.);
 // Static true-peak ceiling protects transients without introducing nonlinear distortion.
 gainDb=std::min(gainDb,ceilingDb-r.inputTruePeak);
 r.appliedGainDb=gainDb;
 const auto scale=std::pow(10.,gainDb/20.);
 for(auto& sample:stereo)sample=float(sample*scale);
 r.outputLufs=integratedLoudnessApprox(stereo,rate);
 r.outputTruePeak=estimatedTruePeakDb(stereo,rate);
 return r;
}
std::vector<Clip> sliceClipAtTransients(const Clip& clip,double sensitivity,double minSliceSeconds,size_t maxSlices,double projectBpm){
 if(clip.stereo.empty())return {};
 if(maxSlices==0||maxSlices>4096||minSliceSeconds<=0||minSliceSeconds>10||projectBpm<=0||projectBpm>400)
  throw std::invalid_argument("Invalid slicing parameters");
 auto times=detectTransients(clip.stereo,clip.sampleRate,sensitivity,minSliceSeconds);
 const auto frames=clip.stereo.size()/2;
 std::vector<size_t> indices{0};
 for(const auto time:times){
  const auto idx=std::min(frames,size_t(std::llround(time*clip.sampleRate)));
  if(idx>indices.back()+size_t(minSliceSeconds*clip.sampleRate)&&idx<frames-size_t(.02*clip.sampleRate)){
   indices.push_back(idx);if(indices.size()>=maxSlices)break;
  }
 }
 indices.push_back(frames);
 std::vector<Clip> slices;
 for(size_t s=0;s+1<indices.size();++s){
  Clip out=clip;
  out.name=clip.name+" slice "+std::to_string(s+1);
  out.stereo.assign(clip.stereo.begin()+std::ptrdiff_t(indices[s]*2),clip.stereo.begin()+std::ptrdiff_t(indices[s+1]*2));
  out.waveformPeaks.clear();out.fadeInSeconds=out.fadeOutSeconds=0;
  out.sourcePath.clear();
  out.startBeat=clip.startBeat+double(indices[s])/clip.sampleRate*(projectBpm/60.);
  buildWaveformPeaks(out);
  slices.push_back(std::move(out));
 }
 return slices;
}
}
