#include "AudioCore.h"
#include "MidiTools.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#define VERIFY(e) do{if(!(e)){std::cerr<<"FAILED "<<__FILE__<<":"<<__LINE__<<" "<<#e<<"\n";std::abort();}}while(false)
int main(){
 using namespace veysona;
 EqSettings eq;
 for(auto hz:{40.,200.,1000.,10000.,20000.})VERIFY(std::abs(eqMagnitudeDb(eq,hz,48000))<.00001);
 eq.bands[0].gainDb=9;
 VERIFY(eqMagnitudeDb(eq,35,48000)>6);
 VERIFY(std::abs(eqMagnitudeDb(eq,13000,48000))<2);
 eq.bands[0].gainDb=0;eq.bands[1].gainDb=-10;
 VERIFY(eqMagnitudeDb(eq,360,48000)<-8);
 VERIFY(std::abs(eqMagnitudeDb(eq,15000,48000))<1);
 eq.bands[1].gainDb=0;
 eq.bands[6].gainDb=8;
 VERIFY(eqMagnitudeDb(eq,4800,48000)>6);
 eq.bands[6].gainDb=0;
 auto c=makeEqCoefficients(eq.bands[1],48000);BiquadState l;for(int i=0;i<48000;++i){auto v=l.process(i==0?1.f:0.f,c);VERIFY(std::isfinite(v));}
 Session s;s.bpm=120;Track t;t.gain=1;t.cutoff=1;
 Clip a;a.stereo.assign(48000*2,.5f);a.sampleRate=48000;t.clips.push_back(a);s.tracks.push_back(t);
 auto plain=s.render(.5);
 s.tracks[0].eq.bands[3].gainDb=-18; auto lowHigh=s.render(.5);
 VERIFY(plain.size()==lowHigh.size());
 // DC / near-DC is intentionally largely unchanged by the high shelf.
 VERIFY(std::abs(plain[9000]-lowHigh[9000])<.06f);
 s.tracks[0].eq.bands[3].gainDb=0;
 s.tracks[0].compressor.enabled=true;s.tracks[0].compressor.thresholdDb=-35;s.tracks[0].compressor.ratio=12;s.tracks[0].compressor.attackMs=1;
 auto compressed=s.render(.5);VERIFY(std::abs(compressed[35000])<std::abs(plain[35000])*.7f);
 // Fades change both ends of the audio while leaving the centre stable.
 s.tracks[0].compressor.enabled=false;
 s.tracks[0].clips[0].fadeInSeconds=.1f;s.tracks[0].clips[0].fadeOutSeconds=.1f;
 const auto faded=s.render(1.05);
 VERIFY(std::abs(faded[0])<.01f);
 VERIFY(std::abs(faded[20000])>.1f);
 VERIFY(std::abs(faded[(48000-1)*2])<.02f);
 std::vector<Note> notes{{60,.13,.39,.8f},{64,.63,.37,.7f}};
 midi::quantize(notes,.25,true);VERIFY(notes[0].beat==.25);VERIFY(notes[1].beat==.75);VERIFY(notes[0].length>=.25);
 midi::transpose(notes,12);VERIFY(notes[0].pitch==72);
 midi::duplicate(notes,4);VERIFY(notes.size()==4);VERIFY(notes.back().beat>=4);
 midi::humanize(notes,.04,.1f,1224);for(const auto& n:notes)VERIFY(n.beat>=0&&n.velocity>0&&n.velocity<=1);
 auto prior=notes; midi::humanize(prior,0.,0.f,1);VERIFY(prior.size()==notes.size());
 std::cout<<"PASS: eight-band EQ, biquad stability, compressor gain reduction, MIDI quantize/transpose/duplicate/humanize\n";
}
