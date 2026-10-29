#include "AudioCore.h"
#include "ProductionAnalysis.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){
 const fs::path root=VEYSONA_SOURCE_PATH;
 size_t samples=0,instruments=0,midi=0;
 for(const auto category:{"samples","instruments"}){
  for(const auto& f:fs::directory_iterator(root/"assets"/category)){
   if(f.path().extension()!=".wav")continue;
   auto c=veysona::readWav(f.path().string());
   check(c.stereo.size()>=4096&&c.sampleRate==48000,"invalid waveform sample");
   if(std::string(category)=="samples")++samples;else ++instruments;
  }
 }
 for(const auto& f:fs::directory_iterator(root/"assets/midi")){
  if(f.path().extension()!=".mid")continue;
  std::ifstream in(f.path(),std::ios::binary);char h[4]{};in.read(h,4);
  check(in.gcount()==4&&std::string(h,4)=="MThd","bad MIDI header");++midi;
 }
 check(samples>=157&&instruments>=48&&midi>=30,"v1.4 sound bank incomplete");
 check(fs::exists(root/"assets/branding/Veysona-Logo.png"),"missing brand logo");
 check(fs::exists(root/"src/ProductionAnalysis.cpp"),"missing production tools");
 check(fs::exists(root/"tools/generate_v14_content.py"),"missing source asset generation");
 check(fs::exists(root/"RELEASE_NOTES_v1.4.md"),"missing release notes");
 const auto cmake=root/"CMakeLists.txt";
 std::ifstream input(cmake);std::string text((std::istreambuf_iterator<char>(input)),{});
 check(text.find("VERSION 1.4.0")!=std::string::npos || text.find("VERSION 1.5.0")!=std::string::npos || text.find("VERSION 1.6.0")!=std::string::npos || text.find("VERSION 1.7.0")!=std::string::npos || text.find("VERSION 1.8.0")!=std::string::npos || text.find("VERSION 1.9.0")!=std::string::npos || text.find("VERSION 1.11.0")!=std::string::npos || text.find("VERSION 1.12.0")!=std::string::npos || text.find("VERSION 1.13.0")!=std::string::npos || text.find("VERSION 1.14.0")!=std::string::npos || text.find("VERSION 1.15.0")!=std::string::npos || text.find("VERSION 1.16.0")!=std::string::npos || text.find("VERSION 1.17.0")!=std::string::npos || text.find("VERSION 1.18.0")!=std::string::npos || text.find("VERSION 1.19.0")!=std::string::npos || text.find("VERSION 1.20.0")!=std::string::npos || text.find("VERSION 1.24.0")!=std::string::npos || text.find("VERSION 1.25.0")!=std::string::npos || text.find("VERSION 1.31.0")!=std::string::npos,"incorrect CMake version");
 std::cout<<"v1.4 release verified: "<<samples<<" WAV samples, "<<instruments
  <<" instrument presets, "<<midi<<" MIDI patterns\n";
}
