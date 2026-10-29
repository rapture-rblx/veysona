#include "MixWorkflow.h"
#include "MidiTools.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace veysona;
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL: "<<#x<<" line "<<__LINE__<<"\n";return 1;}}while(false)
int main(){
 for(float db:{-70.f,-48.f,-24.f,-12.f,-6.f,0.f,3.f,6.f}){
  const float gain=mix::amplitude(db),fader=mix::faderFromGain(gain);
  CHECK(fader>=0.f&&fader<=1.f);
  CHECK(std::abs(mix::decibels(mix::gainFromFader(fader))-db)<.0003f);
 }
 CHECK(mix::gainFromFader(0.f)==0.f);
 CHECK(mix::faderFromGain(0.f)==0.f);
 CHECK(mix::amplitude(-100.f)==0.f);
 Track a,b,c;
 mix::setGain(a,1000.f);CHECK(a.gain<=2.0f);
 mix::setGain(a,std::numeric_limits<float>::quiet_NaN());CHECK(a.gain==0.f);
 mix::setPan(a,-10.f);CHECK(a.pan==-1.f);
 mix::setPan(a,std::numeric_limits<float>::infinity());CHECK(a.pan==0.f);
 CHECK(!mix::setOutput(a,1,1));CHECK(mix::setOutput(a,0,1));CHECK(a.outputBus==0);
 CHECK(mix::setOutput(a,-1,1));CHECK(a.outputBus==-1);
 CHECK(!mix::setSend(a,3,.4f,2));CHECK(mix::setSend(a,1,2.f,2));CHECK(a.sends[1]==1.f);
 CHECK(mix::setSend(a,1,-1.f,2));CHECK(a.sends[1]==0.f);
 std::vector<Track> tracks{a,b,c};
 mix::toggleSolo(tracks,1,true);CHECK(tracks[1].solo&&!tracks[0].solo);
 CHECK(!mix::audible(tracks[0],true));CHECK(mix::audible(tracks[1],true));
 mix::toggleSolo(tracks,2,false);CHECK(tracks[1].solo&&tracks[2].solo);
 mix::toggleSolo(tracks,1,true);CHECK(!tracks[1].solo&&!tracks[2].solo);
 tracks[1].gain=.25f;tracks[1].pan=-.6f;tracks[1].cutoff=.3f;tracks[1].eq.enabled=true;
 tracks[1].sends[0]=.7f;tracks[1].outputBus=0;
 tracks[1].clips.resize(1);tracks[1].midi.resize(1);tracks[1].instrumentSample={.3f,.4f};
 auto settings=mix::copyChannel(tracks[1]);
 auto originalClips=tracks[0].clips.size();auto originalMidi=tracks[0].midi.size();
 mix::pasteChannel(tracks[0],settings,2);
 CHECK(std::abs(tracks[0].gain-.25f)<.0001f&&tracks[0].pan==-.6f);
 CHECK(tracks[0].eq.enabled&&tracks[0].sends[0]==.7f&&tracks[0].outputBus==0);
 CHECK(tracks[0].clips.size()==originalClips&&tracks[0].midi.size()==originalMidi);
 mix::pasteChannel(tracks[2],settings,0);CHECK(tracks[2].outputBus==-1&&tracks[2].sends[0]==0.f);
 std::vector<Note> notes{{61,.35,.5,.5f},{69,1.18,.5,.2f}};
 const auto original=notes;
 midi::quantizeStrength(notes,.25,0.f);CHECK(std::abs(notes[0].beat-original[0].beat)<1.e-10);
 midi::quantizeStrength(notes,.25,.5f);CHECK(std::abs(notes[0].beat-.3)<1.e-10);
 midi::quantizeStrength(notes,.25,1.f);CHECK(std::abs(notes[0].beat-.25)<1.e-10);
 CHECK(midi::nearestScalePitch(61,0,false)==60);
 CHECK(midi::nearestScalePitch(63,0,false)==62);
 CHECK(midi::nearestScalePitch(63,0,true)==63);
 CHECK(midi::nearestScalePitch(127,0,false)==127);
 midi::constrainToNamedScale(notes,0,false);CHECK(notes[0].pitch==60);
 midi::velocityCurve(notes,.5f);CHECK(notes[0].velocity>.5f&&notes[0].velocity<1.f);
 midi::velocityCurve(notes,1.f,2.f);CHECK(notes[0].velocity<=1.f);
 std::cout<<"PASS: v1.15 mixer fader, solo, routing, send and scale-aware MIDI editing\n";
}
