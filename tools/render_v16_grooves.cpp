#include "AudioCore.h"
#include "GrooveLab.h"
#include "SessionHealth.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>
using namespace veysona;
namespace fs=std::filesystem;
static void w16(std::ostream& s,uint16_t v){s.put(char(v>>8));s.put(char(v&255));}
static void w32(std::ostream& s,uint32_t v){for(int k=24;k>=0;k-=8)s.put(char(v>>k));}
static void varlen(std::vector<uint8_t>& b,uint32_t v){
 uint8_t tmp[5];int n=0;tmp[n++]=uint8_t(v&127);
 while((v>>=7)!=0)tmp[n++]=uint8_t((v&127)|128);
 while(n)b.push_back(tmp[--n]);
}
struct Event {uint32_t tick;int pitch,velocity;bool isOn;};
static void saveMidi(const fs::path& file,const std::vector<Note>& notes){
 std::vector<Event> events;
 for(const auto& n:notes){
  auto start=uint32_t(std::llround(n.beat*480.));
  auto end=std::max(start+1,uint32_t(std::llround((n.beat+n.length)*480.)));
  events.push_back({start,n.pitch,std::clamp(int(std::round(n.velocity*127.)),1,127),true});
  events.push_back({end,n.pitch,0,false});
 }
 std::sort(events.begin(),events.end(),[](const Event& a,const Event& b){
  if(a.tick!=b.tick)return a.tick<b.tick;
  if(a.isOn!=b.isOn)return !a.isOn;
  return a.pitch<b.pitch;
 });
 std::vector<uint8_t> body={0,255,81,3,7,161,32}; // 120 BPM
 uint32_t last=0;
 for(const auto& e:events){varlen(body,e.tick-last);last=e.tick;
  body.insert(body.end(),{uint8_t(e.isOn?0x99:0x89),uint8_t(e.pitch),uint8_t(e.velocity)});
 }
 body.insert(body.end(),{0,255,47,0});
 std::ofstream out(file,std::ios::binary|std::ios::trunc);
 if(!out)throw std::runtime_error("Cannot create MIDI file: "+file.string());
 out.write("MThd",4);w32(out,6);w16(out,0);w16(out,1);w16(out,480);
 out.write("MTrk",4);w32(out,uint32_t(body.size()));
 out.write(reinterpret_cast<const char*>(body.data()),std::streamsize(body.size()));
 if(!out)throw std::runtime_error("MIDI write failure");
}
int main(int argc,char** argv){
 try{
  if(argc!=4){std::cerr<<"Usage: veysona_v16_grooves ASSETS_DIRECTORY OUTPUT_MIDI_DIRECTORY OUTPUT_DEMO.wav\n";return 2;}
  const fs::path assets=argv[1],midiDir=argv[2],output=argv[3];
  fs::create_directories(midiDir);
  Session song;song.sampleRate=48000;song.bpm=120;song.master.gain=.77f;
  Track drums;drums.name="Veysona Groove Lab";drums.instrument=Instrument::DrumRack;drums.gain=.60f;
  for(const auto& [slot,note,name]:std::vector<std::tuple<size_t,int,const char*>>{
   {0,36,"Kick - Deep.wav"},{1,38,"Snare - Clean.wav"},
   {2,42,"HiHat - Closed.wav"},{3,46,"HiHat - Open.wav"}}){
    auto clip=readWav((assets/name).string());assignDrumSample(drums,slot,note,clip);
  }
  const auto styles=std::vector<std::pair<groove::Style,std::string>>{
   {groove::Style::House,"House"},{groove::Style::Garage,"UK Garage"},
   {groove::Style::Breakbeat,"Breakbeat"},{groove::Style::Trap,"Trap"}};
  for(size_t i=0;i<styles.size();++i){
   groove::Settings settings;settings.style=styles[i].first;settings.bars=4;
   settings.swing=styles[i].first==groove::Style::Garage?.2:.12;
   settings.seed=13579u+uint32_t(i);
   MidiRegion region;region.startBeat=double(i)*16.;region.lengthBeats=16.;
   region.name=styles[i].second+" Groove";region.notes=groove::generate(settings);
   saveMidi(midiDir/("V16 Groove - "+styles[i].second+" 120BPM.mid"),region.notes);
   drums.midi.push_back(std::move(region));
  }
  song.tracks.push_back(std::move(drums));
  const auto status=health::inspect(song);
  if(!status.canExport())throw std::runtime_error(status.summary());
  const auto sound=song.render(32.);
  writeWavAdvanced(output.string(),sound,song.sampleRate,WavFormat::PCM24);
  std::cout<<"Generated 4 original MIDI grooves and 32-second 24-bit WAV demo: "<<output<<"\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
