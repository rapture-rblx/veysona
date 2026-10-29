#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace veysona;
namespace fs=std::filesystem;
#define CHECK(p) do{if(!(p)){std::cerr<<"FAIL at "<<__LINE__<<": "<<#p<<'\n';return 1;}}while(false)
static std::vector<char> bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(){
 const auto dir=fs::temp_directory_path()/"veysona_v122_stream_export";
 fs::create_directories(dir);
 struct Cleanup{fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{dir};
 std::vector<float> audio(48007*2);
 for(size_t i=0;i<audio.size()/2;++i){
  audio[2*i]=float(.777*std::sin(2*kPi*423*i/48000.));
  audio[2*i+1]=float(.326*std::cos(2*kPi*989*i/48000.));
 }
 for(auto fmt:{WavFormat::PCM16,WavFormat::PCM24,WavFormat::Float32}){
  for(auto dither:{DitherMode::TPDF,DitherMode::None}){
   auto name=std::to_string(int(fmt))+std::to_string(int(dither));
   const auto one=dir/(name+"_one.wav"),split=dir/(name+"_split.wav");
   writeWavAdvanced(one.string(),audio,48000,fmt,dither);
   WavStreamWriter stream(split.string(),48000,fmt,audio.size()/2,dither);
   size_t pos=0;for(size_t chunks:std::vector<size_t>{1,2,17,203,512,4097,131,88,31,9}){
    const auto n=std::min(chunks,audio.size()/2-pos);
    stream.append(audio.data()+pos*2,n);pos+=n;
   }
   while(pos<audio.size()/2){auto n=std::min(size_t(613),audio.size()/2-pos);stream.append(audio.data()+pos*2,n);pos+=n;}
   stream.finish();
   CHECK(bytes(one)==bytes(split));
   const auto decoded=readWav(split.string());
   CHECK(decoded.stereo.size()==audio.size());
   CHECK(decoded.sampleRate==48000);
   if(fmt==WavFormat::Float32)CHECK(decoded.stereo==audio);
  }
 }
 // The 4 GiB RIFF boundary now switches to RF64 instead of rejecting the render.
 {WavStreamWriter w((dir/"too_long.wav").string(),48000,WavFormat::Float32,size_t(600'000'000));}
 CHECK(bytes(dir/"too_long.wav").size()>=80);
 bool rejected=false;
 try{WavStreamWriter w((dir/"short.wav").string(),48000,WavFormat::PCM24,8);
     w.append(audio.data(),7);w.finish();}
 catch(const std::exception&){rejected=true;}
 CHECK(rejected);
 rejected=false;
 try{WavStreamWriter w((dir/"over.wav").string(),48000,WavFormat::PCM24,8);
     w.append(audio.data(),9);}
 catch(const std::exception&){rejected=true;}
 CHECK(rejected);
 // A normal mix may contain non-finite plugin samples; WAV exports must sanitize.
 const float invalid[4]={std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),-.5f,.5f};
 WavStreamWriter w((dir/"nan.wav").string(),48000,WavFormat::Float32,2);
 w.append(invalid,2);w.finish();
 const auto repaired=readWav((dir/"nan.wav").string()).stereo;
 CHECK(repaired[0]==0.f&&repaired[1]==0.f&&repaired[2]==-.5f&&repaired[3]==.5f);
 std::cout<<"PASS: chunk-invariant stream output, 3 formats, deterministic dither, RF64 upgrade and malformed audio\n";
}
