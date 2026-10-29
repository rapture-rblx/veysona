#include "SessionHealth.h"
#include "GrooveLab.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#define REQUIRE(v) do { if(!(v)) {std::cerr << "FAILED: " << #v << " at line " << __LINE__ << "\n";std::abort();} } while(0)
int main(){
 using namespace veysona;
 Session song;song.bpm=125;song.sampleRate=48000;
 Track drums;drums.name="Drums";drums.instrument=Instrument::DrumRack;
 MidiRegion region;region.name="Groove";region.lengthBeats=16;
 groove::Settings settings;settings.style=groove::Style::House;settings.bars=4;settings.seed=12345;
 region.notes=groove::generate(settings);
 REQUIRE(!region.notes.empty());
 bool kick0=false,snare1=false;
 for(const auto& n:region.notes){
  REQUIRE(n.beat>=0&&n.beat<16.);REQUIRE(n.length>0);REQUIRE(n.velocity>0&&n.velocity<=1);
  if(n.pitch==36&&n.beat==0)kick0=true;
  if(n.pitch==38&&std::abs(n.beat-1.)<1e-7)snare1=true;
 }
 REQUIRE(kick0&&snare1);
 REQUIRE(region.notes.size()==groove::generate(settings).size());
 auto repeated=groove::generate(settings);
 for(size_t i=0;i<region.notes.size();++i) REQUIRE(region.notes[i].pitch==repeated[i].pitch && region.notes[i].beat==repeated[i].beat && region.notes[i].velocity==repeated[i].velocity);
 drums.midi.push_back(region);song.tracks.push_back(drums);
 auto healthy=health::inspect(song); REQUIRE(healthy.errors()==0);REQUIRE(healthy.canExport());
 song.tracks[0].outputBus=42;REQUIRE(health::inspect(song).errors()>0);
 song.tracks[0].outputBus=-1;
 song.tracks[0].midi[0].notes[0].velocity=2;REQUIRE(health::inspect(song).errors()>0);
 song.tracks[0].midi[0].notes[0].velocity=.9f;
 Clip clip;clip.name="Test";clip.stereo={0.1f,0.1f};
 song.tracks[0].clips.push_back(clip);REQUIRE(health::inspect(song).canExport());
 song.tracks[0].clips[0].stereo[0]=std::numeric_limits<float>::quiet_NaN();
 REQUIRE(health::inspect(song).errors()>0);
 song.tracks[0].clips[0].stereo[0]=.1f;
 song.loopStartBeat=8;song.loopEndBeat=4;REQUIRE(health::inspect(song).errors()>0);
 song.loopStartBeat=0;song.loopEndBeat=0;
 song.master.gain=std::numeric_limits<float>::infinity();REQUIRE(health::inspect(song).errors()>0);song.master.gain=.85f;
 song.bpm=std::numeric_limits<double>::infinity();REQUIRE(health::inspect(song).errors()>0);
 for(auto style:{groove::Style::Garage,groove::Style::Breakbeat,groove::Style::Trap}){
  settings.style=style;settings.bars=16;settings.stepsPerBar=32;settings.density=1.0;
  auto notes=groove::generate(settings);
  REQUIRE(!notes.empty()&&notes.size()<16*32*5);
  for(const auto& n:notes)REQUIRE(n.beat>=0&&n.beat<64);
 }
 std::cout << "Workflow tests passed: four rhythm styles, deterministic MIDI, project diagnostics and validation\n";
}
