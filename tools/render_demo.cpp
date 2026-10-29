#include "AudioCore.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"Usage: veysona_demo_render SAMPLE_DIRECTORY OUTPUT.wav\n";return 2;}
 try{
  std::filesystem::path dir=argv[1];
  veysona::Session s;s.sampleRate=48000;s.bpm=120;s.beatsPerBar=4;
  auto load=[&](const char* file){return veysona::readWav((dir/file).string());};
  auto addRhythm=[&](const char* name,const char* filename,int step,int phase,float gain){
   veysona::Track t;t.name=name;t.gain=gain;
   const auto sample=load(filename);
   for(int beat=phase;beat<32;beat+=step){auto clip=sample;clip.startBeat=beat;clip.name=name;t.clips.push_back(std::move(clip));}
   s.tracks.push_back(std::move(t));
  };
  addRhythm("Kick","Kick - Club.wav",2,0,.75f);
  addRhythm("Snare","Snare - Clean.wav",2,1,.45f);
  addRhythm("Hi Hats","HiHat - Closed.wav",1,0,.47f);
  veysona::Track bass;bass.name="Analog Bass";bass.instrument=veysona::Instrument::Square;
  bass.gain=.6f;bass.eq.bands[0].gainDb=3;bass.eq.bands[3].gainDb=-8;
  veysona::MidiRegion bassNotes;bassNotes.name="Bass Sequence";bassNotes.lengthBeats=32;
  const int bassLine[]={36,36,43,39,41,41,43,38};
  for(int i=0;i<32;++i)bassNotes.notes.push_back({bassLine[i%8],double(i),.72,.58f});
  bass.midi.push_back(std::move(bassNotes));s.tracks.push_back(std::move(bass));
  veysona::Track keys;keys.name="Synth Chords";keys.instrument=veysona::Instrument::Sine;
  keys.gain=.46f;keys.delayMix=.15f;keys.eq.bands[1].gainDb=-2;
  veysona::MidiRegion chords;chords.name="Minor Chords";chords.lengthBeats=32;
  const int chordPitches[4][3]={{60,63,67},{56,60,63},{58,62,65},{55,59,62}};
  for(int bar=0;bar<8;++bar)for(int n:chordPitches[bar%4])chords.notes.push_back({n,bar*4.,3.6,.65f});
  keys.midi.push_back(std::move(chords));s.tracks.push_back(std::move(keys));
  auto audio=s.render(16.2);
  veysona::writeWav(argv[2],audio,s.sampleRate);
  std::cout<<"Rendered 16.2s Veysona original demo, "<<s.tracks.size()<<" tracks, "<<audio.size()/2<<" frames\n";
 }catch(const std::exception& e){std::cerr<<"Demo render failed: "<<e.what()<<"\n";return 1;}
}
