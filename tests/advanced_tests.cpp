#ifdef NDEBUG
#undef NDEBUG // Assertions must be active in Release test executables.
#endif
#include "AudioCore.h"
#include "MidiTools.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <array>
#include <algorithm>
using namespace veysona;
static float energy(const std::vector<float>& a){double sum=0;for(auto x:a)sum+=double(x)*x;return float(std::sqrt(sum/std::max(size_t(1),a.size())));}
int main(){
 std::vector<AutomationLane> lanes;
 assert(std::abs(automate(lanes,AutomationTarget::TrackGain,0,.6f)-.6f)<.00001f);
 setAutomationPoint(lanes,AutomationTarget::TrackGain,0,0);
 setAutomationPoint(lanes,AutomationTarget::TrackGain,2,1);
 setAutomationPoint(lanes,AutomationTarget::TrackGain,4,.5f);
 assert(std::abs(automate(lanes,AutomationTarget::TrackGain,1,.6f)-.5f)<1e-5);
 setAutomationPoint(lanes,AutomationTarget::TrackGain,2,.8f);
 assert(lanes[0].points.size()==3);
 assert(std::abs(automate(lanes,AutomationTarget::TrackGain,2,0)-.8f)<1e-5);
 lanes[0].boolStep=true;
 assert(std::abs(automate(lanes,AutomationTarget::TrackGain,1,0))<1e-5);
 lanes[0].boolStep=false;
 Session s;s.sampleRate=48000;s.bpm=120;s.master.gain=1;s.master.limiterEnabled=false;
 Track tr;tr.gain=.5f;Clip c;c.sampleRate=48000;c.stereo.assign(48000*2,.25f);tr.clips.push_back(c);
 s.tracks.push_back(tr);
 auto raw=s.render(1.);
 assert(energy(raw)>.11f&&energy(raw)<.14f);
 s.tracks[0].automation=lanes;
 // Automated gain should decrease the first 0.5 seconds.
 auto autoAudio=s.render(1.);
 assert(energy(autoAudio)<energy(raw));
 s.tracks[0].automation.clear();
 s.buses.push_back({"Drums",false,.5f,0,1,{}});
 s.tracks[0].outputBus=0;
 auto routed=s.render(1.);
 assert(energy(routed)>.05f&&energy(routed)<.07f);
 s.buses[0].muted=true;
 auto silent=s.render(1.);
 assert(energy(silent)<1e-6f);
 s.buses[0].muted=false;s.tracks[0].outputBus=-1;
 s.tracks[0].sends[0]=.5f;
 auto sent=s.render(1.);
 assert(energy(sent)>energy(raw));
 s.tracks[0].sends[0]=0;
 s.tracks[0].gate.enabled=true;s.tracks[0].gate.thresholdDb=0;
 auto gated=s.render(1.);
 assert(energy(gated)<energy(raw)*.02f);
 Clip stretch;c.sampleRate=48000;stretch.sampleRate=48000;
 stretch.stereo.resize(48000*2);
 for(int i=0;i<48000;++i){float v=.3f*std::sin(2*kPi*i*440./48000);stretch.stereo[2*i]=v;stretch.stereo[2*i+1]=v;}
 stretchClipGranular(stretch,.5);
 assert(stretch.stereo.size()==192000);
 for(float v:stretch.stereo)assert(std::isfinite(v)&&std::abs(v)<1.);
 stretchClipGranular(stretch,2);
 assert(stretch.stereo.size()==96000);
 std::array<float,8> l{},r{};for(int i=0;i<8;++i){l[i]=float(i)/10;r[i]=l[i];}
 auto stats=measureAudio(l.data(),r.data(),8);assert(stats.peak>.69&&stats.peak<.71&&stats.correlation>.999);
 veysona::midi::Recorder recorder;
 std::vector<Note> recording;
 recorder.noteOn(60,1,0,.6f);recorder.noteOn(60,2,.5,.8f);
 recorder.noteOff(60,2,1.5,recording);recorder.noteOff(60,1,2,recording);
 assert(recording.size()==2&&recording[0].channel==2&&recording[1].channel==1);
 assert(std::abs(recording[0].length-1.)<1.e-6);
 std::vector<Note> arp{{60,0,1,.8f},{64,0,1,.8f},{67,0,1,.8f}};
 veysona::midi::addArpeggio(arp,.25,2,true);
 assert(arp.size()==6 && arp[0].pitch==60 && arp[3].pitch==72);
 veysona::midi::addChordInversions(arp,1);assert(arp.size()==6);
 std::vector<MidiControllerEvent> cc{{MidiControlType::CC,1,64,1,.26},{MidiControlType::PitchWheel,0,8192,1,.73}};
 veysona::midi::quantizeControllers(cc,.25);
 assert(std::abs(cc[0].beat-.25)<1.e-6 && std::abs(cc[1].beat-.75)<1.e-6);
 std::cout<<"Automation, routing, bus mute/sends, gate, waveform warp and stats passed\n";
}
