#ifdef NDEBUG
#undef NDEBUG // Assertions must be active in Release test executables.
#endif
#include "AudioCore.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
using namespace veysona;
static double difference(const std::vector<float>& a,const std::vector<float>& b){double diff=0;for(size_t i=0;i<a.size();++i)diff+=std::abs(double(a[i])-b[i]);return diff/std::max(size_t(1),a.size());}
int main(){
 const auto dir=std::filesystem::temp_directory_path();
 std::vector<float> source(96000);for(size_t i=0;i<source.size()/2;++i){const float x=.4f*std::sin(float(2*kPi*440*i/48000.));source[2*i]=x;source[2*i+1]=x*.5f;}
 for(const auto format:{WavFormat::PCM16,WavFormat::PCM24,WavFormat::Float32}){
  auto file=(dir/(format==WavFormat::PCM16?"odaw-test16.wav":format==WavFormat::PCM24?"odaw-test24.wav":"odaw-test32f.wav")).string();
  writeWavAdvanced(file,source,48000,format);auto result=readWav(file);
  assert(result.sampleRate==48000&&result.stereo.size()==source.size());
  const auto tolerance=format==WavFormat::PCM16?0.00008:0.000002;
  assert(difference(source,result.stereo)<tolerance);
  std::filesystem::remove(file);
 }
 Session s;s.sampleRate=48000;s.bpm=120;s.master.gain=1;s.master.limiterEnabled=false;
 Track t;t.gain=1;t.cutoff=1;t.eq.enabled=false;t.compressor.enabled=false;Clip clip;clip.stereo=source;clip.sampleRate=48000;t.clips.push_back(clip);s.tracks.push_back(t);
 const auto original=s.render(1.);
 auto run=[&](auto change){s.tracks[0].creativeFx={};change(s.tracks[0].creativeFx);auto audio=s.render(1.);for(auto v:audio)assert(std::isfinite(v));return difference(original,audio);};
 assert(run([](auto& f){f.chorusEnabled=true;f.chorusMix=.8f;f.chorusDepthMs=6.f;})>.005);
 assert(run([](auto& f){f.autopanEnabled=true;f.autopanDepth=1.f;f.autopanPeriodBeats=1.f;})>.005);
 assert(run([](auto& f){f.crusherEnabled=true;f.crusherBits=4;f.crusherHoldFrames=15;f.crusherMix=1.f;})>.005);
 std::cout<<"24-bit/float WAV round trip and three creative DSP effects passed\n";
}
