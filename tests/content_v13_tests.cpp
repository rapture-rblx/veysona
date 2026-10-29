#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__FILE__<<":"<<__LINE__<<" "<<#x<<"\n";return 1;}}while(false)
namespace fs=std::filesystem;
int main(){
 const auto root=fs::path(VEYSONA_SOURCE_PATH);
 size_t nSamples=0,nInstruments=0,nMidi=0;
 for(const auto folder:{"samples","instruments"}){
  for(const auto& item:fs::directory_iterator(root/"assets"/folder)){
   if(item.path().extension()!=".wav")continue;
   const auto clip=veysona::readWav(item.path().string());
   CHECK(clip.sampleRate>=44100&&clip.stereo.size()>=4096);
   CHECK(clip.stereo.size()%2==0&&!clip.waveformPeaks.empty());
   double energy=0;
   for(float x:clip.stereo){CHECK(std::isfinite(x)&&std::abs(x)<=1.001);energy+=x*x;}
   CHECK(energy>1e-8);
   (std::string(folder)=="samples"?nSamples:nInstruments)++;
  }
 }
 for(const auto& item:fs::directory_iterator(root/"assets"/"midi")){
  if(item.path().extension()!=".mid")continue;
  std::ifstream input(item.path(),std::ios::binary);
  char header[14]={};input.read(header,14);
  CHECK(input.gcount()==14&&std::string(header,4)=="MThd");
  CHECK(uint8_t(header[7])==6&&std::string(header+14,0)=="");
  ++nMidi;
 }
 CHECK(nSamples>=133&&nInstruments>=40&&nMidi>=18);
 CHECK(fs::exists(root/"tools"/"generate_v13_content.py"));
 CHECK(fs::exists(root/"assets"/"branding"/"Veysona-Logo.png"));
 std::cout<<"Verified "<<nSamples<<" audio samples, "<<nInstruments<<" instrument presets, "<<nMidi<<" MIDI files\n";
}
