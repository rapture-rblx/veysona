#include "AudioCore.h"
#include "ProductionAnalysis.h"
#include "MidiTools.h"
#include <filesystem>
#include <iostream>
using namespace veysona;
int main(int argc,char** argv){
 try {
  const std::string output=argc>1?argv[1]:"Veysona_v1.4_Production_Demo_24bit.wav";
  Session s;s.sampleRate=48000;s.bpm=120;s.master.gain=.8f;s.master.ceilingDb=-1.f;
  Track drums;drums.name="Rhythm Lab";drums.instrument=Instrument::DrumRack;drums.gain=.58f;
  const std::filesystem::path source=std::filesystem::path(__FILE__).parent_path().parent_path()/"assets/samples";
  for(const auto& [slot,note,file]:std::vector<std::tuple<size_t,int,std::string>>{{0,36,"V14 Kick - Deep Tube.wav"},{1,38,"V14 Snare - Bright Plate.wav"},{2,42,"V14 Hat - Satin Closed.wav"},{3,46,"V14 Hat - Vinyl Open.wav"}}){
   auto clip=readWav((source/file).string());assignDrumSample(drums,slot,note,clip);
  }
  MidiRegion dr;dr.name="24/7 Beats";dr.startBeat=0;dr.lengthBeats=32;
  dr.notes=midi::drumStepPattern(8,4,211,.48);drums.midi.push_back(std::move(dr));s.tracks.push_back(std::move(drums));
  Track keys;keys.name="Chord Studio";keys.instrument=Instrument::Triangle;keys.gain=.42f;
  keys.room.enabled=true;keys.room.mix=.12f;keys.eq.bands[0].gainDb=1.6f;
  MidiRegion ch;ch.name="I V vi IV";ch.lengthBeats=32;
  ch.notes=midi::chordProgression(60,{1,5,6,4,1,5,6,4},false,4.,.55f);
  keys.midi.push_back(std::move(ch));s.tracks.push_back(std::move(keys));
  Track bass;bass.name="Bass Synth";bass.instrument=Instrument::Saw;bass.gain=.3f;
  bass.eq.bands[3].gainDb=-7.0f;
  MidiRegion bas;bas.name="Scale-aware Bass";bas.lengthBeats=32;
  bas.notes=midi::basslineFromChords(s.tracks[1].midi[0].notes,.5,57);
  bass.midi.push_back(std::move(bas));s.tracks.push_back(std::move(bass));
  Track lead;lead.name="Melody Lab";lead.instrument=Instrument::Sine;lead.gain=.3f;
  lead.room.enabled=true;lead.room.mix=.27f;
  MidiRegion melody;melody.name="Arpeggio";melody.lengthBeats=32;
  melody.notes=midi::chordProgression(72,{1,5,6,4},false,4.,.52f);
  midi::addArpeggio(melody.notes,.5,2,true);
  for(auto& note:melody.notes)note.beat+=16.;
  lead.midi.push_back(std::move(melody));s.tracks.push_back(std::move(lead));
  auto audio=s.render(16.);
  const auto mastering=production::normalizeLoudnessOffline(audio,s.sampleRate,-15.,-1.);
  writeWavAdvanced(output,audio,s.sampleRate,WavFormat::PCM24);
  std::cout<<"Rendered "<<output<<" (16s, stereo 48kHz / 24bit): approx "
   <<mastering.outputLufs<<" LUFS, true peak "<<mastering.outputTruePeak<<" dBTP\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
