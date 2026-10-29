#include "AudioCore.h"
#include "AdvancedMidi.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace veysona;
static void w16(std::ostream& o,uint16_t n){for(int j=0;j<2;++j)o.put(char(n>>(8*j)));}
static void w32(std::ostream& o,uint32_t n){for(int j=0;j<4;++j)o.put(char(n>>(8*j)));}
static void w64(std::ostream& o,uint64_t n){w32(o,uint32_t(n));w32(o,uint32_t(n>>32));}
static void fmtStereoFloat(std::ostream& o,bool extensible){
 o.write("fmt ",4);w32(o,extensible?40:16);w16(o,extensible?0xfffe:3);
 w16(o,2);w32(o,48000);w32(o,384000);w16(o,8);w16(o,32);
 if(extensible){
  w16(o,22);w16(o,32);w32(o,3); // CB size, valid bits, FL/FR mask
  const uint8_t guid[]={3,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
  o.write(reinterpret_cast<const char*>(guid),16);
 }
}
static void samples(std::ostream& o){
 for(float value:{.25f,-.5f,.75f,-.125f}){
  uint32_t bits;static_assert(sizeof(bits)==sizeof(value));
  std::memcpy(&bits,&value,4);w32(o,bits);
 }
}
static std::vector<char> bytes(const std::filesystem::path& p){
 std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
static void require(bool good,const char* text){if(!good)throw std::runtime_error(text);}
int main(){
 auto dir=std::filesystem::temp_directory_path()/"veysona_v124_media_test";
 std::filesystem::create_directories(dir);
 const auto extended=dir/"extensible.wav";
 {
  std::ofstream o(extended,std::ios::binary);
  o.write("RIFF",4);w32(o,60+16);o.write("WAVE",4);
  fmtStereoFloat(o,true);o.write("data",4);w32(o,16);samples(o);
 }
 auto clip=readWav(extended.string());
 require(clip.sampleRate==48000&&clip.stereo.size()==4,"Extensible WAV not decoded");
 require(clip.stereo[0]==.25f&&clip.stereo[1]==-.5f&&clip.stereo[2]==.75f&&clip.stereo[3]==-.125f,"Extensible stereo lost fidelity");
 const auto rf64=dir/"rf64.wav";
 {
  std::ofstream o(rf64,std::ios::binary);
  o.write("RF64",4);w32(o,0xffffffffu);o.write("WAVEds64",8);
  w32(o,28);w64(o,72+16);w64(o,16);w64(o,2);w32(o,0);
  fmtStereoFloat(o,false);o.write("data",4);w32(o,0xffffffffu);samples(o);
 }
 clip=readWav(rf64.string());
 require(clip.stereo.size()==4&&clip.stereo[2]==.75f,"RF64 small-file import failed");
 // No 4 GiB test allocation: inspect only the RF64 header of a declared large stream.
 const auto declared=dir/"large.wav";
 {
  WavStreamWriter writer(declared.string(),48000,WavFormat::Float32,size_t(600'000'000));
  auto head=bytes(declared); // stream is not necessarily flushed yet: no assumption about bytes
  (void)head;
 }
 auto hdr=bytes(declared);
 require(hdr.size()>=80&&std::string(hdr.data(),4)=="RF64","Large export not automatically RF64");
 require(std::string(hdr.data()+12,4)=="ds64","RF64 ds64 metadata missing");
 require(std::string(hdr.data()+72,4)=="data","RF64 chunk layout invalid");
 const auto broken=dir/"broken.wav";
 {
  std::ofstream o(broken,std::ios::binary);
  o.write("RF64",4);w32(o,0xffffffffu);o.write("WAVEdata",8);w32(o,0xffffffffu);
 }
 bool rejected=false;try{(void)readWav(broken.string());}catch(const std::exception&){rejected=true;}
 require(rejected,"Malformed RF64 not rejected");
 // Duplicate removal must treat MIDI channels as separate voices.
 advanced_midi::Notes dup{{60,0,1,.8f,1},{60,0,1,.8f,2},{60,0,1,.8f,1}};
 advanced_midi::removeDuplicates(dup);require(dup.size()==2,"MIDI channel identity lost");
 advanced_midi::Notes notes{{60,0,.25,.8f,1},{64,0,.25,.8f,1},{60,1,.25,.8f,1},{60,1.5,.25,.8f,2}};
 advanced_midi::makePolyphonicLegato(notes,4.);
 require(notes[0].length==1.&&notes[1].length==4.&&notes[2].length==4.&&notes[3].length==4.,"Polyphonic legato cut unrelated voice");
 notes[0].length=8.;notes[1].length=6.;notes[2].length=2.;
 advanced_midi::resolveSameVoiceOverlaps(notes);
 require(notes[0].length==1.&&notes[1].length==6.&&notes[2].length==2.,"Repeated-note overlap repair changed other voices");
 std::filesystem::remove_all(dir);
 std::cout<<"PASS: extensible PCM/float container, RF64 export/import, invalid RF64 guards, channel-aware MIDI transformations\n";
}
