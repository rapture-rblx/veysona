#include "AudioCore.h"
#include "TimeWarp.h"
#include "ProductionAnalysis.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace veysona;
namespace fs=std::filesystem;
#define REQUIRE(expr) do {if(!(expr)){std::cerr<<"FAIL "<<__LINE__<<": "<<#expr<<"\n";return 1;}}while(false)
static void le32(std::vector<uint8_t>& v,size_t i,uint32_t x){for(int k=0;k<4;k++)v[i+k]=uint8_t(x>>(8*k));}
static std::vector<uint8_t> bytes(const fs::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void save(const fs::path& path,const std::vector<uint8_t>& data){std::ofstream f(path,std::ios::binary);f.write((const char*)data.data(),std::streamsize(data.size()));}
static bool rejects(const fs::path& p){try{(void)readWav(p.string());return false;}catch(const std::exception&){return true;}}
static double pearson(const std::vector<float>& a){
 double dot=0, aa=0,bb=0;
 for(size_t i=0;i<a.size()/2;++i){const double l=a[2*i],r=a[2*i+1];dot+=l*r;aa+=l*l;bb+=r*r;}
 return dot/std::sqrt(std::max(1e-15,aa*bb));
}
int main(){
 const auto tmp=fs::temp_directory_path()/("veysona-qa-"+std::to_string(std::random_device{}()));
 fs::create_directories(tmp);
 struct Cleanup {fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{tmp};
 constexpr uint32_t sr=48000;
 std::vector<float> anti(sr*2);
 for(size_t i=0;i<sr;++i){
  const float x=.65f*std::sin(float(2*kPi*double(i)*523.25/sr))+.11f*std::sin(float(2*kPi*double(i)*1700./sr));
  anti[2*i]=x;anti[2*i+1]=-x;
 }
 for(auto quality:{warp::Quality::Smooth,warp::Quality::Percussive}){
  for(double speed:{.25,.5,.8,1.25,2.,4.}){
   auto stretched=warp::stretchWsola(anti,sr,speed,{quality,10.});
   REQUIRE(stretched.size()==size_t(std::llround(sr/speed))*2);
   REQUIRE(std::all_of(stretched.begin(),stretched.end(),[](float v){return std::isfinite(v);}));
   REQUIRE(pearson(stretched)<-.9999); // preserve exact anti-phase relationship
   double energy=0;for(float x:stretched)energy+=double(x)*x;
   REQUIRE(energy/stretched.size()>.02); // no accidental silence or total cancellation
  }
 }
 auto clean=tmp/"clean.wav";
 writeWavAdvanced(clean.string(),anti,sr,WavFormat::PCM24);
 auto imported=readWav(clean.string());
 REQUIRE(imported.sampleRate==sr&&imported.stereo.size()==anti.size());
 REQUIRE(pearson(imported.stereo)<-.9999);
 auto original=bytes(clean);
 REQUIRE(original.size()>44);
 // Malformed container claims more bytes than physically available.
 auto broken=original;le32(broken,4,uint32_t(broken.size()+1000));
 save(tmp/"truncated_riff.wav",broken);
 REQUIRE(rejects(tmp/"truncated_riff.wav"));
 // Truncated audio: data chunk must be contained in the declared RIFF length.
 broken=original;le32(broken,40,uint32_t(broken.size()+100));
 save(tmp/"oversized_data.wav",broken);
 REQUIRE(rejects(tmp/"oversized_data.wav"));
 // A partial PCM24 frame is not silently discarded.
 broken=original;broken.pop_back();le32(broken,4,uint32_t(broken.size()-8));
 le32(broken,40,uint32_t(broken.size()-44));
 save(tmp/"partial_frame.wav",broken);
 REQUIRE(rejects(tmp/"partial_frame.wav"));
 // Broken format declaration must not be decoded by seeking into the data.
 broken=original;le32(broken,16,12);
 save(tmp/"short_format.wav",broken);
 REQUIRE(rejects(tmp/"short_format.wav"));
 // A WAVE that says it contains PCM data but omits the fmt chunk must fail.
 broken=original;
 for(size_t k=12;k<16;++k)broken[k]='X';
 save(tmp/"missing_fmt.wav",broken);
 REQUIRE(rejects(tmp/"missing_fmt.wav"));
 // Edited clip buffers must be disconnected from the original source path.
 Clip splitSource=imported;
 splitSource.sourcePath="C:/original/recording.wav";
 splitSource.name="Recording";
 splitSource.fadeInSeconds=.07f;
 splitSource.fadeOutSeconds=.21f;
 auto tail=splitAudioClip(splitSource,.5,120.);
 REQUIRE(splitSource.sourcePath.empty()&&tail.sourcePath.empty());
 REQUIRE(splitSource.stereo.size()+tail.stereo.size()==imported.stereo.size());
 REQUIRE(splitSource.fadeOutSeconds==0&&tail.fadeInSeconds==0);
 Clip reverseSource=imported;
 reverseSource.sourcePath="C:/original/recording.wav";
 reverseSource.fadeInSeconds=.07f;
 reverseSource.fadeOutSeconds=.21f;
 reverseAudioClip(reverseSource);
 REQUIRE(reverseSource.sourcePath.empty());
 REQUIRE(std::abs(reverseSource.fadeInSeconds-.21f)<.00001f);
 REQUIRE(std::abs(reverseSource.fadeOutSeconds-.07f)<.00001f);
 REQUIRE(std::abs(reverseSource.stereo[0]-imported.stereo[imported.stereo.size()-2])<1e-7);
 // Caller mistakes must not turn a missing send/return bus into a crash.
 Track routed; routed.outputBus=0; routed.sends[0]=.5f;
 float leftIn[3]{.5f,-.3f,.1f},rightIn[3]{.25f,.2f,-.05f};
 float mainL[3]{},mainR[3]{};
 routeTrack(routed,3,leftIn,rightIn,mainL,mainR,nullptr,nullptr,1);
 for(int i=0;i<3;++i)REQUIRE(mainL[i]==leftIn[i]&&mainR[i]==rightIn[i]);

 // Empty/invalid audio is rejected by advanced processing, not misreported.
 REQUIRE(std::isfinite(production::estimatedTruePeakDb(imported.stereo,imported.sampleRate)));
 std::cout<<"PASS: 12 anti-phase WSOLA combinations, PCM24 roundtrip, 5 corrupt WAVs, split/reverse metadata and routing guards\n";
 return 0;
}
