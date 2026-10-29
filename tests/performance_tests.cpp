#ifdef NDEBUG
#undef NDEBUG // Assertions must be active in Release test executables.
#endif
#include "Performance.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <array>
using namespace veysona;
static bool quiet(const float* a,int n){for(int i=0;i<n;++i)if(std::abs(a[i])>1e-6f)return false;return true;}
int main(){
 Session session;session.sampleRate=48000;session.bpm=120;
 Track t;t.name="Drum loop";t.gain=1;
 Clip c;c.sampleRate=48000;c.name="Test loop";
 c.stereo.assign(4800*2,.4f);t.clips.push_back(c);
 session.tracks.push_back(t);
 Track midi;midi.instrument=Instrument::Sine;MidiRegion r;r.lengthBeats=1;r.notes.push_back({69,0,.5,.8f,1});midi.midi.push_back(r);
 session.tracks.push_back(midi);
 PerformanceLauncher launcher;launcher.prepare(2);launcher.setQuantisation(1);
 launcher.queue(0,LaunchKind::Audio,0,.3);launcher.queue(1,LaunchKind::Midi,0,.3);
 assert(std::abs(launcher.state(0).queuedForBeat-1.)<1e-8);
 std::array<float,512> l{},rbuf{};
 std::array<PerformanceMidiEvent,64> midiEvents{};
 auto eventCount=launcher.collectMidiEvents(session,1,23900,512,midiEvents.data(),64);
 assert(eventCount>=1);
 assert(midiEvents[0].type==PerformanceMidiType::NoteOn);
 assert(midiEvents[0].offset==100);
 launcher.renderTrack(session,0,23900,512,l.data(),rbuf.data());
 assert(quiet(l.data(),100));assert(l[120]>.39f);
 assert(launcher.state(0).active.kind==LaunchKind::Audio);
 launcher.renderTrack(session,0,24250,512,l.data(),rbuf.data());assert(l[0]>.39f);
 launcher.renderTrack(session,1,24500,512,l.data(),rbuf.data());
 bool audible=false;for(auto v:l)audible|=std::abs(v)>.001f;assert(audible);
 // MIDI note-offs happen in the loop, and all-notes-off is sent at scene transitions.
 eventCount=launcher.collectMidiEvents(session,1,36000,512,midiEvents.data(),64);
 assert(eventCount>=1);
 assert(midiEvents[0].type==PerformanceMidiType::NoteOff);
 launcher.queue(0,LaunchKind::Stop,-1,1.6);
 launcher.renderTrack(session,0,47900,512,l.data(),rbuf.data());assert(l[50]>.39f);assert(quiet(l.data()+110,402));
 launcher.queueScene(session,0,2.5);
 assert(launcher.state(0).hasQueue&&launcher.state(1).hasQueue);
 assert(std::abs(launcher.state(1).queuedForBeat-3.)<1e-8);
 launcher.renderTrack(session,0,72000,512,l.data(),rbuf.data());assert(l[0]>.39f);
 session.tracks[0].mute=true;launcher.renderTrack(session,0,73000,512,l.data(),rbuf.data());assert(quiet(l.data(),512));
 session.tracks[0].mute=false;
 session.tracks[1].solo=true;launcher.renderTrack(session,0,74000,512,l.data(),rbuf.data());assert(quiet(l.data(),512));
 session.tracks[1].solo=false;
 launcher.stopAll(4);
 eventCount=launcher.collectMidiEvents(session,1,96000,512,midiEvents.data(),64);
 assert(eventCount==16&&midiEvents[0].type==PerformanceMidiType::AllNotesOff);
 launcher.renderTrack(session,0,96000,512,l.data(),rbuf.data());assert(quiet(l.data(),512));
 // Capture a launched scene as fixed-length arrangement material.
 auto count=commitPerformanceScene(session,0,16.,8.);
 assert(count==2);
 assert(session.tracks[0].clips.size()==2);
 assert(std::abs(session.tracks[0].clips.back().startBeat-16.)<1e-8);
 assert(session.tracks[0].clips.back().stereo.size()==size_t(8*60./120.*48000)*2);
 assert(session.tracks[1].midi.size()==2);
 assert(session.tracks[1].midi.back().notes.size()==8);
 assert(std::abs(session.tracks[1].midi.back().startBeat-16.)<1e-8);
 // Invalid commands never mutate launch state.
 bool threw=false;try{launcher.queue(10,LaunchKind::Audio,0,0);}catch(const std::out_of_range&){threw=true;}assert(threw);
 std::cout<<"Quantised scene/audio/MIDI launching, clip looping, stop, mute and solo passed\n";
}
