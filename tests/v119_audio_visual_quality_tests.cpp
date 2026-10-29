#include "TimeWarp.h"
#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#define VERIFY(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
using namespace veysona;
static std::vector<float> stereoSine(double frequency,size_t frames,uint32_t sr,float amplitude=.75f){
 std::vector<float> out(frames*2);
 for(size_t i=0;i<frames;++i){
  const auto v=float(amplitude*std::sin(2.*kPi*frequency*double(i)/sr));
  out[2*i]=v;out[2*i+1]=-v;
 }
 return out;
}
static double rms(const std::vector<float>& a,size_t start=0){
 double total=0;size_t count=0;
 for(size_t i=std::min(start*2,a.size());i<a.size();i+=2){
  total+=double(a[i])*a[i];++count;
 }
 return std::sqrt(total/std::max(size_t(1),count));
}
static double measuredTone(const std::vector<float>& a,double frequency,uint32_t sr){
 const size_t n=a.size()/2;const size_t skip=std::min(size_t(200),n/10);
 double sinPart=0,cosPart=0;
 for(size_t i=skip;i+skip<n;++i){
  const double phase=2.*kPi*frequency*double(i)/sr;
  sinPart+=a[2*i]*std::sin(phase);
  cosPart+=a[2*i]*std::cos(phase);
 }
 const double nUsed=double(n-2*skip);
 return 2.*std::hypot(sinPart,cosPart)/nUsed;
}
static double luminance(uint32_t hex){
 const auto channel=[](double x){x/=255.;return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);};
 return .2126*channel(double((hex>>16)&255))+.7152*channel(double((hex>>8)&255))+.0722*channel(double(hex&255));
}
static double contrast(uint32_t a,uint32_t b){
 const double x=luminance(a),y=luminance(b);
 return (std::max(x,y)+.05)/(std::min(x,y)+.05);
}
int main(){
 constexpr uint32_t sr=48000;
 const auto in=stereoSine(4000.,size_t(sr)/2,sr);
 for(const auto rate:{.5,.75,1.25,1.5,2.}){
  const auto output=warp::resamplePitchBandlimited(in,rate);
  VERIFY(output.size()==size_t(std::llround(double(in.size()/2)/rate))*2);
  VERIFY(std::all_of(output.begin(),output.end(),[](float x){return std::isfinite(x);}));
  for(size_t j=0;j<output.size();j+=2)VERIFY(std::abs(output[j]+output[j+1])<2e-5f);
  const double amplitude=measuredTone(output,4000.*rate,sr);
  VERIFY(amplitude>.69&&amplitude<.79); // +/- 0.6 dB passband at these ratios
 }
 // Raising pitch an octave moves input tones >12kHz above 24kHz Nyquist.
 // A linear resampler aliases a 17kHz tone back to 14kHz at nearly full level.
 const auto hf=stereoSine(17000.,sr/2,sr);
 const auto down=warp::resamplePitchBandlimited(hf,2.);
 const double aliased=measuredTone(down,14000.,sr);
 VERIFY(aliased<.025); // >29 dB reduction vs .75 full-scale aliased tone
 // DC and low-frequency signals should have stable gain and no new clipping.
 std::vector<float> dc(sr/4*2,.234f);
 const auto dcUp=warp::resamplePitchBandlimited(dc,.5);
 const auto dcDown=warp::resamplePitchBandlimited(dc,2.);
 for(auto* p:{&dcUp,&dcDown}){
  for(float value:*p)VERIFY(std::isfinite(value)&&std::abs(value-.234f)<1e-4f);
 }
 // Existing full pitch path still keeps duration, even with the improved FIR.
 Clip clip;clip.sampleRate=sr;clip.stereo=in;
 warp::pitchShiftClip(clip,7.);
 VERIFY(clip.stereo.size()==in.size());
 VERIFY(std::all_of(clip.stereo.begin(),clip.stereo.end(),[](float x){return std::isfinite(x);}));
 // Accessible contrast of actual v1.19 UI palette values.
 constexpr uint32_t panel=0x182637,ink=0xf2f7fe,muted=0xbed0e2,raised=0x293b51;
 VERIFY(contrast(ink,panel)>12.);
 VERIFY(contrast(muted,panel)>8.);
 VERIFY(contrast(muted,raised)>6.);
 bool invalid=false;try{(void)warp::resamplePitchBandlimited({},2.);}catch(const std::invalid_argument&){invalid=true;}
 VERIFY(invalid);
 std::cout<<"PASS: five stereo-linked pitch ratios; passband; octave-up alias suppression "
          <<aliased<<"; DC stability; duration; UI text contrast\n";
}
