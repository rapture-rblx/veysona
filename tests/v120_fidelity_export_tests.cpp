#include "AudioCore.h"
#include "TimeWarp.h"
#include "MixWorkflow.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>

#define REQUIRE(p) do { if(!(p)){std::cerr<<"FAIL "<<__LINE__<<": "<<#p<<'\n';return 1;} } while(false)
using namespace veysona;
static std::vector<char> readBytes(const std::filesystem::path& path){
 std::ifstream file(path,std::ios::binary);
 return {std::istreambuf_iterator<char>{file},std::istreambuf_iterator<char>{}};
}
int main(){
 const uint32_t sr=48000;
 const auto dir=std::filesystem::temp_directory_path()/"veysona_v120_fidelity_test";
 std::filesystem::create_directories(dir);
 std::vector<float> silent(12000*2,0.f);
 writeWavAdvanced((dir/"dither.wav").string(),silent,sr,WavFormat::PCM16,DitherMode::TPDF);
 writeWavAdvanced((dir/"plain.wav").string(),silent,sr,WavFormat::PCM16,DitherMode::None);
 const auto dithered=readWav((dir/"dither.wav").string());
 const auto plain=readWav((dir/"plain.wav").string());
 REQUIRE(plain.stereo.size()==silent.size());
 REQUIRE(std::all_of(plain.stereo.begin(),plain.stereo.end(),[](float v){return v==0.f;}));
 size_t nonzero=0;double power=0;
 for(float v:dithered.stereo){if(v!=0)++nonzero;power+=double(v)*v;REQUIRE(std::abs(v)<2.f/32768.f);}
 REQUIRE(nonzero>silent.size()/5&&nonzero<silent.size()*3/5);
 REQUIRE(std::sqrt(power/dithered.stereo.size())<1.f/32768.f);
 const auto original=readBytes(dir/"dither.wav");
 writeWavAdvanced((dir/"dither.wav").string(),silent,sr,WavFormat::PCM16,DitherMode::TPDF);
 REQUIRE(original==readBytes(dir/"dither.wav"));
 // Project-owned audio must be saved as 32-bit float, not round-tripped
 // through PCM16 on every project save. Preserve stereo image and >0 dBFS.
 std::vector<float> project(3000*2);
 for(size_t i=0;i<project.size()/2;++i){
  project[2*i]=float(.873219871*std::sin(2.*kPi*417.*double(i)/sr));
  project[2*i+1]=float(1.37*std::sin(2.*kPi*521.*double(i)/sr));
 }
 writeWavAdvanced((dir/"project.wav").string(),project,sr,WavFormat::Float32);
 auto restored=readWav((dir/"project.wav").string());
 REQUIRE(project==restored.stereo);
 writeWavAdvanced((dir/"project.wav").string(),restored.stereo,sr,WavFormat::Float32);
 REQUIRE(project==readWav((dir/"project.wav").string()).stereo);
 // 24-bit export has TPDF too, without clipping normal mastering peaks.
 writeWavAdvanced((dir/"24.wav").string(),project,sr,WavFormat::PCM24);
 const auto pcm24=readWav((dir/"24.wav").string());
 REQUIRE(pcm24.stereo.size()==project.size());
 REQUIRE(std::abs(pcm24.stereo[100]-project[100])<4.f/8388608.f);
 // Every meter increment must be monotonic in dBFS, not linear amplitude.
 float previous=0;
 for(float db:{-90.f,-60.f,-48.f,-36.f,-24.f,-18.f,-12.f,-6.f,0.f,6.f}){
  const float frac=mix::peakMeterFraction(std::pow(10.f,db/20.f));
  REQUIRE(frac>=previous&&frac<=1.f);
  previous=frac;
 }
 REQUIRE(mix::peakMeterFraction(0.f)==0.f);
 REQUIRE(mix::peakMeterFraction(std::numeric_limits<float>::quiet_NaN())==0.f);
 REQUIRE(!mix::peakClipping(.999f)&&mix::peakClipping(1.f));
 // High-fidelity offline processing must retain exact target length,
 // maintain out-of-phase stereo and preserve the underlying pitch.
 std::vector<float> signal(size_t(sr)*2);
 for(size_t i=0;i<sr;++i){
  const auto sample=float(.48*std::sin(2.*kPi*440.*double(i)/sr));
  signal[i*2]=sample;signal[i*2+1]=-sample;
 }
 for(double speed:{.5,.8,1.25,2.}){
  const auto output=warp::stretchWsola(signal,sr,speed,{warp::Quality::HighFidelity,14.});
  REQUIRE(output.size()/2==size_t(std::llround(double(sr)/speed)));
  for(size_t i=0;i<output.size();i+=2){
   REQUIRE(std::isfinite(output[i])&&std::isfinite(output[i+1]));
   REQUIRE(std::abs(output[i]+output[i+1])<1e-5f);
  }
 }
 std::filesystem::remove_all(dir);
 std::cout<<"PASS: reproducible TPDF, noise floor, lossless float project media, "
          <<"24-bit export, dBFS meters, high-fidelity stereo stretch\n";
}
