#include "AudioCore.h"
#include "MidiTools.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__FILE__<<":"<<__LINE__<<" "<<#x<<"\n";return 1;}}while(false)
using namespace veysona;
static double diff(const std::vector<float>& a,const std::vector<float>& b){
 if(a.size()!=b.size())return 1.e9;
 double d=0;for(size_t i=0;i<a.size();++i)d+=std::abs(double(a[i])-b[i]);return d/a.size();
}
int main(){
 Session session;session.sampleRate=48000;session.bpm=120;session.master.gain=1;
 session.master.limiterEnabled=false;
 Track tr;tr.name="Test signal";tr.gain=1;tr.eq.enabled=false;tr.cutoff=1;
 Clip c;c.sampleRate=48000;c.name="Input";c.stereo.resize(48000*2);
 for(int i=0;i<48000;++i){
  double t=double(i)/48000;
  float val=float((.18*std::sin(2*kPi*250*t)+.18*std::sin(2*kPi*6500*t))*(1.-t*.7));
  c.stereo[i*2]=val;c.stereo[i*2+1]=val*.8f;
 }
 tr.clips.push_back(c);session.tracks.push_back(tr);
 const auto original=session.render(1.);
 session.tracks[0].deEsser={true,5400,-40,.95};
 auto deessed=session.render(1.);CHECK(diff(original,deessed)>.001);
 session.tracks[0].deEsser={};
 session.tracks[0].transient={true,.9f,-.5f};
 auto shaped=session.render(1.);CHECK(diff(original,shaped)>.0002);
 session.tracks[0].transient={};
 session.tracks[0].room={true,.8f,.4f,.6f};
 auto reverb=session.render(1.2);CHECK(reverb.size()==115200);
 CHECK(diff(original,std::vector<float>(reverb.begin(),reverb.begin()+original.size()))>.001);
 for(float v:reverb)CHECK(std::isfinite(v)&&std::abs(v)<=1.f);
 session.tracks[0].room={};
 auto restored=session.render(1.);CHECK(diff(original,restored)<1e-8);
 // Spectrum FFT peak at 1000 Hz, N=1024 at 48 kHz; bin ~= 21.
 std::vector<float> tone(1024*2);
 for(int i=0;i<1024;++i){const auto x=float(.7*std::sin(2*kPi*1000*i/48000.));tone[i*2]=tone[i*2+1]=x;}
 auto spectrum=analyseSpectrum(tone,0,1024);
 CHECK(spectrum.size()==513);
 auto peak=std::max_element(spectrum.begin()+1,spectrum.end());
 CHECK(std::abs(std::distance(spectrum.begin(),peak)-21)<=1);
 CHECK(*peak>-12.f);
 bool badFft=false;try{analyseSpectrum(tone,0,700);}catch(const std::invalid_argument&){badFft=true;}CHECK(badFft);
 // Comp slicing: deliberately different takes, check edits and equal-power overlap.
 Clip takeA,takeB;takeA.sampleRate=takeB.sampleRate=48000;
 takeA.stereo.assign(48000*2,.25f);takeB.stereo.assign(48000*2,-.25f);
 auto comp=compAudioTakes({takeA,takeB},{{0,0,.5},{1,.5,1.}},.02);
 CHECK(comp.stereo.size()==size_t(2*(48000-960)));
 CHECK(comp.stereo[0]>.24f&&comp.stereo.back()<-.24f);
 CHECK(!comp.waveformPeaks.empty());
 bool badComp=false;try{compAudioTakes({takeA},{{2,0,.3}});}catch(const std::exception&){badComp=true;}CHECK(badComp);
 // Euclidean rhythms: valid hits, note pitch/channel, and spacing.
 auto pattern=midi::makeEuclideanRhythm(16,5,36,.25,2);
 CHECK(pattern.size()==5);for(const auto& n:pattern)CHECK(n.pitch==36&&n.channel==10&&n.beat>=0&&n.beat<4);
 auto zero=midi::makeEuclideanRhythm(8,0,38,.5);CHECK(zero.empty());
 auto all=midi::makeEuclideanRhythm(8,8,38,.5);CHECK(all.size()==8);
 // Deterministic probability selection.
 auto a=all,b=all;midi::applyProbability(a,.5f,456);midi::applyProbability(b,.5f,456);
 CHECK(a.size()==b.size());for(size_t i=0;i<a.size();++i)CHECK(a[i].beat==b[i].beat);
 midi::applyProbability(a,0.f,3);CHECK(a.empty());
 // Chords: strum, velocity ramps, note-repeat and 16th-note groove.
 std::vector<Note> notes={{72,0,1,.8f},{60,0,1,.8f},{67,0,1,.8f}};
 midi::strumChords(notes,.025,true);CHECK(notes.size()==3&&notes[0].pitch==60);
 CHECK(notes[0].beat==0&&notes[1].beat>.0&&notes[2].beat>notes[1].beat);
 midi::velocityRamp(notes,.3f,.95f);CHECK(notes.front().velocity<=.31f&&notes.back().velocity>=.94f);
 midi::repeatNotes(notes,4);CHECK(notes.size()==12);
 std::vector<Note> groove={{60,0,.2,.8f},{62,.25,.2,.8f},{64,.5,.2,.8f}};
 midi::applyGroove(groove,.25,{0,.25,0,-.1});
 CHECK(groove[0].beat==0&&std::abs(groove[1].beat-.3125)<.00001);
 std::cout<<"PASS v1.3: de-esser, transient, reverb, FFT spectrum, comping, Euclidean, probability, strum, repeat, groove\n";
 return 0;
}
