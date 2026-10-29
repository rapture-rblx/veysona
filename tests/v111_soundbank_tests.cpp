#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
#define REQUIRE(e) do{if(!(e))throw std::runtime_error(std::string("FAIL line ")+std::to_string(__LINE__)+": " #e);}while(false)
int main(){
 const auto root=fs::path(VEYSONA_SOURCE_PATH),asset=root/"assets";
 const auto count=[&](const char* folder,const char* extension){size_t n=0;for(const auto& f:fs::directory_iterator(asset/folder))if(f.path().extension()==extension)++n;return n;};
 REQUIRE(count("samples",".wav")>=513);
 REQUIRE(count("instruments",".wav")>=240);
 REQUIRE(count("midi",".mid")>=78);
 size_t effects=0;for(const auto& f:fs::directory_iterator(asset/"effect_presets"))if(f.path().filename().string().starts_with("V11 ") && f.path().string().ends_with(".vfx.json")){std::ifstream file(f.path());std::string s((std::istreambuf_iterator<char>(file)),{});REQUIRE(s.find("veysona-fx-v1")!=std::string::npos);REQUIRE(s.find("compressor")!=std::string::npos);++effects;}
 REQUIRE(effects==72);
 size_t audioAssets=0;double sum=0;
 for(const auto& folder:{"samples","instruments"})for(const auto& f:fs::directory_iterator(asset/folder)){
  if(!f.path().filename().string().starts_with("V11 ")||f.path().extension()!=".wav")continue;
  const auto wav=veysona::readWav(f.path().string());
  REQUIRE(wav.sampleRate==48000u);REQUIRE(!wav.stereo.empty());REQUIRE(!wav.waveformPeaks.empty());
  for(float v:wav.stereo){REQUIRE(std::isfinite(v));REQUIRE(std::abs(v)<=1.001f);sum+=v*v;}
  ++audioAssets;
 }
 REQUIRE(audioAssets==548); // 356 samples + 192 sampled instruments
 REQUIRE(sum>1000.);
 size_t midi=0;for(const auto& f:fs::directory_iterator(asset/"midi")){
  if(!f.path().filename().string().starts_with("V11 "))continue;
  std::ifstream stream(f.path(),std::ios::binary);char buf[4]{};stream.read(buf,4);
  REQUIRE(std::string(buf,4)=="MThd");++midi;
 }REQUIRE(midi==48);
 const auto piano=veysona::readWav((asset/"instruments"/"V11 Velvet Piano C4.wav").string());
 veysona::Track t;veysona::assignSamplerSample(t,piano,60);
 REQUIRE(t.instrument==veysona::Instrument::Sampler);
 t.midi.push_back({});t.midi.back().notes.push_back({60,0,.5,.85f});
 veysona::Session session;session.tracks.push_back(std::move(t));
 auto rendered=session.render(.5);float peak=0;for(auto s:rendered)peak=std::max(peak,std::abs(s));
 REQUIRE(peak>.005f);REQUIRE(peak<=1.001f);
 std::cout<<"Veysona v1.11: "<<audioAssets<<" original new WAVs, "<<effects<<" effect presets, "<<midi<<" MIDI patterns; sampler plays.\n";
}
