// Veysona production tools: portable CLI for offline audio/MIDI processing.
// Separate from the real-time audio callback and the JUCE GUI.
#include "AudioCore.h"
#include "TimeWarp.h"
#include "MidiTools.h"
#include "ProductionAnalysis.h"
#include "ProductionWorkflow.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace veysona;
using namespace veysona::production;

static void usage() {
 std::cout << "Veysona Audio Tools 1.16 (offline, original C++ engine)\n"
           << "Usage:\n"
           << "  veysona_audio_tool analyze INPUT.wav\n"
           << "  veysona_audio_tool master INPUT.wav OUTPUT.wav [target_LUFS=-14] [ceiling_dBTP=-1]\n"
           << "  veysona_audio_tool resample INPUT.wav OUTPUT.wav RATE [taps=64]\n"
           << "  veysona_audio_tool slice INPUT.wav OUTPUT_FOLDER [sensitivity=1.4]\n"
           << "  veysona_audio_tool stretch INPUT.wav OUTPUT.wav SPEED [smooth|drums]\n"
           << "  veysona_audio_tool fit INPUT.wav OUTPUT.wav BEATS PROJECT_BPM\n"
           << "  veysona_audio_tool tempo INPUT.wav OUTPUT.wav SOURCE_BPM TARGET_BPM\n"
           << "  veysona_audio_tool pitch INPUT.wav OUTPUT.wav SEMITONES [-12..12]\n"
           << "  veysona_audio_tool warp INPUT.wav OUTPUT.wav PROJECT_BPM [GRID_BEATS=0.5]\n"
           << "  veysona_audio_tool chords OUTPUT.mid [root_MIDI=60] [BPM=120]\n"
           << "  veysona_audio_tool drums OUTPUT.mid [BPM=120] [seed=1234]\n"
           << "Note: WAV input support follows the Veysona PCM/float decoder.\n"
           << "LUFS and dBTP estimates are approximate, not compliance-certified.\n";
}
static double num(const char* val,double lo,double hi,const std::string& what) {
 size_t pos=0;double n=0;
 try { n=std::stod(val,&pos); } catch (...) {throw std::invalid_argument("Invalid "+what);}
 if(pos!=std::string(val).size()||!std::isfinite(n)||n<lo||n>hi)
  throw std::invalid_argument("Out of range "+what);
 return n;
}
static int integer(const char* val,int lo,int hi,const std::string& what) {
 const auto n=num(val,lo,hi,what);
 if(std::floor(n)!=n)throw std::invalid_argument("Expected integer "+what);
 return static_cast<int>(n);
}
static void safeOutput(const fs::path& input,const fs::path& output) {
 if(input==output || (fs::exists(input)&&fs::exists(output)&&fs::equivalent(input,output)))
  throw std::invalid_argument("Input and output paths must be different");
 if(!output.parent_path().empty()&&!fs::exists(output.parent_path()))
  throw std::invalid_argument("Output folder does not exist: "+output.parent_path().string());
}
static void ensureSuccess(const fs::path& path) {
 if(!fs::exists(path)||fs::file_size(path)==0)throw std::runtime_error("Output file was not created");
}
static void be16(std::ostream& out,uint16_t value){out.put(static_cast<char>(value>>8));out.put(static_cast<char>(value&255));}
static void be32(std::ostream& out,uint32_t value){for(int i=24;i>=0;i-=8)out.put(static_cast<char>((value>>i)&255));}
static void varLen(std::vector<uint8_t>& bytes,uint32_t v) {
 uint8_t tmp[5];size_t n=0;tmp[n++]=static_cast<uint8_t>(v&127);
 while((v>>=7)!=0)tmp[n++]=static_cast<uint8_t>((v&127)|128);
 while(n)bytes.push_back(tmp[--n]);
}
struct MidiMessage {uint32_t tick;bool noteOn;uint8_t channel,pitch,velocity;};
static void writeMidi(const fs::path& file,const std::vector<Note>& notes,int bpm){
 std::vector<MidiMessage> events;events.reserve(notes.size()*2);
 constexpr double ppq=480.;
 for(const auto& note:notes){
  if(note.pitch<0||note.pitch>127||note.channel<1||note.channel>16||!std::isfinite(note.beat)||!std::isfinite(note.length)||!std::isfinite(note.velocity))continue;
  if(note.beat<0||note.length<=0||note.beat>1e6)continue;
  const double end=note.beat+std::max(note.length,1./ppq);
  if(end>1e6)continue;
  const auto startTick=static_cast<uint32_t>(std::llround(note.beat*ppq));
  const auto endTick=std::max(startTick+1,static_cast<uint32_t>(std::llround(end*ppq)));
  const auto vel=static_cast<uint8_t>(std::clamp(int(std::lround(note.velocity*127.)),1,127));
  auto ch=static_cast<uint8_t>(note.channel-1);
  events.push_back({startTick,true,ch,static_cast<uint8_t>(note.pitch),vel});
  events.push_back({endTick,false,ch,static_cast<uint8_t>(note.pitch),0});
 }
 std::sort(events.begin(),events.end(),[](const MidiMessage& a,const MidiMessage& b){
  if(a.tick!=b.tick)return a.tick<b.tick;
  if(a.noteOn!=b.noteOn)return !a.noteOn; // Turn off old voice before retriggering.
  if(a.channel!=b.channel)return a.channel<b.channel;
  return a.pitch<b.pitch;
 });
 std::vector<uint8_t> body={0x00,0xff,0x51,0x03};
 const auto us=static_cast<uint32_t>(std::llround(60000000./bpm));
 body.insert(body.end(),{static_cast<uint8_t>((us>>16)&255),static_cast<uint8_t>((us>>8)&255),static_cast<uint8_t>(us&255)});
 uint32_t previous=0;
 for(auto& e:events){
  varLen(body,e.tick-previous);previous=e.tick;
  body.push_back(static_cast<uint8_t>((e.noteOn?0x90:0x80)|e.channel));
  body.push_back(e.pitch);body.push_back(e.velocity);
 }
 body.insert(body.end(),{0x00,0xff,0x2f,0x00});
 if(body.size()>std::numeric_limits<uint32_t>::max())throw std::length_error("MIDI too large");
 std::ofstream out(file,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot write MIDI");
 out.write("MThd",4);be32(out,6);be16(out,0);be16(out,1);be16(out,480);
 out.write("MTrk",4);be32(out,static_cast<uint32_t>(body.size()));
 out.write(reinterpret_cast<const char*>(body.data()),static_cast<std::streamsize>(body.size()));
 if(!out)throw std::runtime_error("MIDI write failed");
}
int main(int argc,char** argv){
 if(argc<2||std::string(argv[1])=="--help"||std::string(argv[1])=="help") {usage();return argc<2?2:0;}
 try {
  const std::string cmd=argv[1];
  if(cmd=="analyze"&&argc==3){
   auto clip=readWav(argv[2]);
   auto r=analyseLoop(clip.stereo,clip.sampleRate);
   std::cout<<std::fixed<<std::setprecision(2)
            <<"File: "<<argv[2]<<"\nFrames: "<<clip.stereo.size()/2
            <<"\nSample rate: "<<clip.sampleRate<<" Hz\nEstimated BPM: "<<r.estimatedBpm
            <<"\nConfidence: "<<r.confidence<<"\nSample peak: "<<r.peakDbfs
            <<" dBFS\nRMS: "<<r.rmsDbfs<<" dBFS\nApprox loudness: "<<r.approximateLufs
            <<" LUFS\nApprox true peak: "<<r.estimatedTruePeakDbfs<<" dBTP\nSpectral centroid: "
            <<r.spectralCentroidHz<<" Hz\nStereo correlation: "<<r.stereoCorrelation<<"\nL/R balance: "<<r.channelBalanceDb<<" dB\nTransients: "<<r.transientsSeconds.size()<<"\n";
  } else if(cmd=="master"&&(argc==4||argc==5||argc==6)){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const auto target=argc>4?num(argv[4],-40,-5,"target LUFS"):-14.;
   const auto ceiling=argc>5?num(argv[5],-12,0,"ceiling"):-1.;
   auto report=normalizeLoudnessOffline(clip.stereo,clip.sampleRate,target,ceiling);
   writeWavAdvanced(argv[3],clip.stereo,clip.sampleRate,WavFormat::PCM24);ensureSuccess(argv[3]);
   std::cout<<std::fixed<<std::setprecision(2)<<"Gain "<<report.appliedGainDb
            <<" dB; approximate loudness "<<report.outputLufs<<" LUFS; estimated true peak "
            <<report.outputTruePeak<<" dBTP. Saved: "<<argv[3]<<"\n";
  } else if(cmd=="resample"&&(argc==5||argc==6)){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const auto sampleRate=integer(argv[4],8000,384000,"output sample rate");
   const auto taps=argc==6?integer(argv[5],8,256,"taps"):64;
   auto output=resampleSinc(clip.stereo,clip.sampleRate,static_cast<uint32_t>(sampleRate),taps);
   writeWavAdvanced(argv[3],output,static_cast<uint32_t>(sampleRate),WavFormat::PCM24);ensureSuccess(argv[3]);
   std::cout<<"Resampled to "<<sampleRate<<" Hz, "<<output.size()/2<<" frames: "<<argv[3]<<"\n";
  } else if(cmd=="stretch"&&(argc==5||argc==6)){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const double speed=num(argv[4],.25,4.,"speed ratio");
   veysona::warp::Options options;
   if(argc==6){const std::string quality=argv[5];
    if(quality=="drums")options.quality=veysona::warp::Quality::Percussive;
    else if(quality!="smooth")throw std::invalid_argument("Quality must be smooth or drums");
   }
   veysona::warp::stretchClip(clip,speed,options);
   writeWavAdvanced(argv[3],clip.stereo,clip.sampleRate,WavFormat::PCM24);ensureSuccess(argv[3]);
   std::cout<<"Pitch-preserving WSOLA stretch "<<speed<<"x: "<<clip.stereo.size()/2<<" frames: "<<argv[3]<<"\n";
  } else if(cmd=="fit"&&argc==6){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const auto beats=num(argv[4],.25,256.,"beats"),bpm=num(argv[5],20,400,"BPM");
   veysona::warp::fitClipToBeats(clip,beats,bpm);
   writeWavAdvanced(argv[3],clip.stereo,clip.sampleRate,WavFormat::PCM24);ensureSuccess(argv[3]);
   std::cout<<"Fitted clip to "<<beats<<" beats at "<<bpm<<" BPM: "<<argv[3]<<"\n";
  } else if(cmd=="tempo"&&argc==6){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const auto source=num(argv[4],20,400,"source BPM"),target=num(argv[5],20,400,"target BPM");
   veysona::warp::stretchClip(clip,target/source);
   writeWavAdvanced(argv[3],clip.stereo,clip.sampleRate,WavFormat::PCM24);ensureSuccess(argv[3]);
   std::cout<<"Remapped "<<source<<" -> "<<target<<" BPM, pitch preserved: "<<argv[3]<<"\n";
  } else if(cmd=="pitch"&&argc==5){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const auto shift=num(argv[4],-12,12,"semitones");
   veysona::warp::pitchShiftClip(clip,shift);
   writeWavAdvanced(argv[3],clip.stereo,clip.sampleRate,WavFormat::PCM24);ensureSuccess(argv[3]);
   std::cout<<"Shifted pitch by "<<shift<<" semitones: "<<argv[3]<<"\n";
  } else if(cmd=="warp"&&(argc==5||argc==6)){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const auto bpm=num(argv[4],20,400,"BPM");
   const auto subdivision=argc==6?num(argv[5],.125,4.,"grid beats"):.5;
   auto markers=veysona::warp::suggestBeatMarkers(clip,bpm,subdivision);
   if(markers.size()<3)throw std::runtime_error("No reliable transient markers detected");
   veysona::warp::warpClipToMarkers(clip,markers,{veysona::warp::Quality::Percussive,6.});
   writeWavAdvanced(argv[3],clip.stereo,clip.sampleRate,WavFormat::PCM24);ensureSuccess(argv[3]);
   std::cout<<"Beat-grid warp markers: "<<markers.size()<<" saved to "<<argv[3]<<"\n";
  } else if(cmd=="slice"&&(argc==4||argc==5)){
   auto clip=readWav(argv[2]);const auto output=fs::path(argv[3]);
   if(!fs::is_directory(output))throw std::invalid_argument("Existing output directory required");
   const auto sensitivity=argc==5?num(argv[4],.1,10.,"sensitivity"):1.4;
   auto slices=sliceClipAtTransients(clip,sensitivity,.1,128);
   if(slices.empty())throw std::runtime_error("No audio to slice");
   for(size_t i=0;i<slices.size();++i){
    const auto dest=output/("Veysona-Slice-"+std::to_string(i+1)+".wav");
    if(fs::exists(dest))throw std::runtime_error("Output already exists: "+dest.string());
   }
   for(size_t i=0;i<slices.size();++i){
    const auto dest=output/("Veysona-Slice-"+std::to_string(i+1)+".wav");
    writeWavAdvanced(dest.string(),slices[i].stereo,slices[i].sampleRate,WavFormat::PCM24);
    ensureSuccess(dest);
   }
   std::cout<<"Saved "<<slices.size()<<" slices to "<<output.string()<<"\n";
  } else if(cmd=="chords"&&(argc>=3&&argc<=5)){
   const auto root=argc>3?integer(argv[3],24,100,"root MIDI"):60;
   const auto bpm=argc>4?integer(argv[4],20,400,"BPM"):120;
   auto notes=midi::chordProgression(root,{1,5,6,4},false,4.);
   writeMidi(argv[2],notes,bpm);ensureSuccess(argv[2]);
   std::cout<<"Generated "<<notes.size()<<" MIDI chord notes: "<<argv[2]<<"\n";
  } else if(cmd=="drums"&&(argc>=3&&argc<=5)){
   const auto bpm=argc>3?integer(argv[3],20,400,"BPM"):120;
   const auto seed=argc>4?integer(argv[4],0,1000000,"seed"):1234;
   auto notes=midi::drumStepPattern(4,4,static_cast<uint32_t>(seed),.6);
   writeMidi(argv[2],notes,bpm);ensureSuccess(argv[2]);
   std::cout<<"Generated "<<notes.size()<<" MIDI drum notes: "<<argv[2]<<"\n";
  } else if(cmd=="transcribe"&&(argc==4||argc==5)){
   safeOutput(argv[2],argv[3]);auto clip=readWav(argv[2]);
   const auto bpm=argc==5?integer(argv[4],20,400,"BPM"):120;
   auto notes=workflow::transcribeMonophonic(clip,bpm);
   if(notes.notes.empty())throw std::runtime_error("No monophonic notes detected");
   writeMidi(argv[3],notes.notes,bpm);ensureSuccess(argv[3]);
   std::cout<<"Wrote "<<notes.notes.size()<<" editable monophonic MIDI notes to "<<argv[3]<<"\n";
  } else if(cmd=="template"&&(argc==4||argc==5)){
   const std::string style=argv[2];workflow::Template kind;
   if(style=="house")kind=workflow::Template::House;
   else if(style=="dnb")kind=workflow::Template::DrumAndBass;
   else if(style=="hiphop")kind=workflow::Template::HipHop;
   else if(style=="ambient")kind=workflow::Template::Ambient;
   else throw std::invalid_argument("Unknown template name");
   const auto duration=argc==5?num(argv[4],.5,60.,"seconds"):12.;
   auto song=workflow::createTemplate(kind,48000);
   auto sound=song.render(duration);
   writeWavAdvanced(argv[3],sound,song.sampleRate,WavFormat::PCM24);
   ensureSuccess(argv[3]);
   std::cout<<"Rendered "<<style<<" template at "<<song.bpm<<" BPM: "<<argv[3]<<"\n";
  } else {usage();throw std::invalid_argument("Unknown command or wrong number of arguments");}
  return 0;
 } catch(const std::exception& e){std::cerr<<"Veysona: "<<e.what()<<"\n";return 1;}
}
