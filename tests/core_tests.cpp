#include "AudioCore.h"
#include <cstdlib>
#define CHECK(expr) do { if(!(expr)){ std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #expr << "\n";std::abort(); } } while(0)
#include <cmath>
#include <iostream>
#include <filesystem>
int main(){
 using namespace veysona;
 Session s;s.bpm=120;s.sampleRate=48000;Track track;track.name="Bass";
 Clip c;c.name="Test";c.sampleRate=48000;c.startBeat=4;c.stereo.assign(480*2,.6f);track.clips.push_back(c);s.tracks.push_back(track);
 CHECK(std::abs(s.duration()-2.01)<.001);
 EngineState fx;fx.prepare(1,48000);float a[512],b[512];renderTrackBlock(s,0,0,512,a,b,fx);
 for(auto x:a)CHECK(x==0);
 fx.reset();renderTrackBlock(s,0,96000,512,a,b,fx);CHECK(a[0]>.1f);
 s.bpm=60;fx.reset();renderTrackBlock(s,0,96000,512,a,b,fx);CHECK(a[0]==0); // tempo moves the clip on beat grid
 s.bpm=120;s.tracks[0].mute=true;renderTrackBlock(s,0,96000,512,a,b,fx);CHECK(a[0]==0);
 s.tracks[0].mute=false;s.tracks[0].clips.clear();MidiRegion m;m.lengthBeats=4;m.notes.push_back({69,0,1,.8f});s.tracks[0].midi.push_back(m);
 fx.reset();renderTrackBlock(s,0,100,512,a,b,fx);float peak=0;for(auto x:a)peak=std::max(peak,std::abs(x));CHECK(peak>.03f);
 auto demo=makeDemoSound(0,48000);CHECK(demo.size()==96000);
 auto path=std::filesystem::temp_directory_path()/"veysona_test.wav";writeWav(path.string(),demo,48000);auto back=readWav(path.string());CHECK(back.stereo.size()==demo.size());CHECK(std::abs(back.stereo[200]-demo[200])<.0001f);std::filesystem::remove(path);
 auto rendered=s.render(2);CHECK(rendered.size()==192000);
 std::cout<<"All audio-core tests passed (tempo, sync, mute, MIDI synth, export, WAV import)\n";
}
