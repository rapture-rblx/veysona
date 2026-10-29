#include "SessionHealth.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <limits>

namespace veysona::health {
int Report::errors() const { return int(std::count_if(findings.begin(), findings.end(), [](const Finding& f){return f.severity == Severity::Error;})); }
int Report::warnings() const { return int(std::count_if(findings.begin(), findings.end(), [](const Finding& f){return f.severity == Severity::Warning;})); }
std::string Report::summary() const {
 std::ostringstream out;
 out << errors() << " error(s), " << warnings() << " warning(s)";
 if(findings.empty()) out << "; project checks passed";
 for(const auto& f : findings) {
  const char* severity = f.severity == Severity::Error ? "ERROR" : f.severity == Severity::Warning ? "WARNING" : "INFO";
  out << "\n[" << severity << "] " << f.location << ": " << f.message;
 }
 return out.str();
}
Report inspect(const Session& s) {
 Report report;
 auto flag = [&](Severity sev, const std::string& code, const std::string& where, const std::string& description) {
  report.findings.push_back({sev,code,where,description});
 };
 if(s.sampleRate < 8000 || s.sampleRate > 384000) flag(Severity::Error,"sample_rate","Project","Invalid project sample rate");
 if(!std::isfinite(s.bpm) || s.bpm < 20.0 || s.bpm > 400.0) flag(Severity::Error,"tempo","Project","Tempo must be between 20 and 400 BPM");
 if(s.beatsPerBar < 1 || s.beatsPerBar > 32) flag(Severity::Error,"meter","Project","Beats per bar is outside the supported range");
 if(!std::isfinite(s.loopStartBeat) || !std::isfinite(s.loopEndBeat) || s.loopStartBeat < 0 || s.loopEndBeat < 0 ||
    (s.loopEndBeat > 0 && s.loopEndBeat <= s.loopStartBeat))
  flag(Severity::Error,"loop","Project","Loop markers are invalid or reversed");
 if(!std::isfinite(s.master.gain) || s.master.gain<0 || s.master.gain>8 ||
    !std::isfinite(s.master.ceilingDb) || s.master.ceilingDb>0 || s.master.ceilingDb<-30)
  flag(Severity::Error,"master","Project","Invalid master gain or limiter ceiling");
 for(std::size_t bi=0;bi<s.buses.size();++bi){
  const auto& b=s.buses[bi];
  if(!std::isfinite(b.gain)||b.gain<0||b.gain>8||!std::isfinite(b.pan)||std::abs(b.pan)>1||
     !std::isfinite(b.width)||b.width<0||b.width>4)
   flag(Severity::Error,"bus_mixer","Bus "+std::to_string(bi+1),"Invalid gain, pan, or stereo width");
 }
 if(s.tracks.empty()) flag(Severity::Warning,"empty_project","Project","No tracks; add an audio or MIDI track");
 if(s.tracks.size() > 512) flag(Severity::Warning,"large_project","Project","More than 512 tracks; performance may be affected");
 if(s.buses.size() > kMaxBuses) flag(Severity::Error,"bus_count","Project","Too many output buses");
 // Detect automation corruption before saving/production export; the audio engine
 // expects finite, chronological points for binary-search interpolation.
 auto inspectAutomation=[&](const std::vector<AutomationLane>& lanes,const std::string& where){
  if(lanes.size()>16)flag(Severity::Warning,"automation_count",where,"More than 16 automation lanes may be truncated on save");
  for(const auto& lane:lanes){
   if(int(lane.target)<0||int(lane.target)>int(AutomationTarget::BusGain)){
    flag(Severity::Error,"automation_target",where,"Unsupported automation target");continue;
   }
   double last=-1.;
   for(const auto& point:lane.points){
    if(!std::isfinite(point.beat)||!std::isfinite(point.value)||point.beat<0||point.beat>1e6||point.beat<last){
     flag(Severity::Error,"automation_points",where,"Automation points must be finite and ordered");break;
    }
    last=point.beat;
   }
   if(lane.points.size()>8192)flag(Severity::Warning,"automation_capacity",where,"Lane exceeds saved automation point capacity");
  }
 };
 for(std::size_t bi=0;bi<s.buses.size();++bi)inspectAutomation(s.buses[bi].automation,"Bus "+std::to_string(bi+1));
 for(std::size_t ti=0;ti<s.tracks.size();++ti){
  const auto& t=s.tracks[ti];
  const std::string where="Track " + std::to_string(ti+1) + " (" + t.name + ")";
  inspectAutomation(t.automation,where);
  if(t.outputBus < -1 || (t.outputBus >= 0 && std::size_t(t.outputBus) >= s.buses.size()))
   flag(Severity::Error,"output_bus",where,"Output bus does not exist");
  if(!std::isfinite(t.gain) || t.gain < 0 || t.gain > 8 || !std::isfinite(t.pan) || std::abs(t.pan) > 1.f)
   flag(Severity::Error,"mixer",where,"Invalid gain or pan setting");
  for(std::size_t k=0;k<kMaxBuses;++k){
   if(!std::isfinite(t.sends[k]) || t.sends[k]<0 || t.sends[k]>1)
    flag(Severity::Error,"send",where,"A send level is outside 0..1");
   else if(k >= s.buses.size() && t.sends[k]>0)
    flag(Severity::Warning,"missing_send_bus",where,"Send targets a bus that has not been created");
  }
  if(t.pluginChain.size()>8) flag(Severity::Warning,"plugin_chain",where,"More than eight insert plugins may be unsupported by the current GUI");
  for(std::size_t ci=0;ci<t.clips.size();++ci){
   const auto& c=t.clips[ci]; const auto clipWhere=where+" / Audio "+std::to_string(ci+1);
   if(c.stereo.empty()) flag(Severity::Warning,"empty_clip",clipWhere,"Audio clip has no samples");
   if(c.stereo.size()%2) flag(Severity::Error,"stereo",clipWhere,"Interleaved stereo sample count is odd");
   if(c.sampleRate<8000 || c.sampleRate>384000) flag(Severity::Error,"clip_rate",clipWhere,"Invalid clip sample rate");
   if(!std::isfinite(c.startBeat)||c.startBeat<0 || !std::isfinite(c.gain)||c.gain<0 || c.gain>8)
    flag(Severity::Error,"clip_position",clipWhere,"Invalid audio clip position or gain");
   if(!std::isfinite(c.fadeInSeconds)||!std::isfinite(c.fadeOutSeconds)||c.fadeInSeconds<0 || c.fadeOutSeconds<0)
    flag(Severity::Error,"clip_fade",clipWhere,"Invalid fade duration");
   if(c.stereo.size()>2*48000*60*15ull) flag(Severity::Warning,"long_clip",clipWhere,"Long audio clip may use substantial memory");
   // This audit is explicitly offline; inspect all samples so corruption is not missed.
   for(size_t n=0;n<c.stereo.size();++n){if(!std::isfinite(c.stereo[n])){
    flag(Severity::Error,"nonfinite_audio",clipWhere,"Audio samples contain NaN or infinity");break;
   }}
  }
  for(std::size_t mi=0;mi<t.midi.size();++mi){
   const auto& m=t.midi[mi];const auto regionWhere=where+" / MIDI "+std::to_string(mi+1);
   if(!std::isfinite(m.startBeat)||m.startBeat<0||!std::isfinite(m.lengthBeats)||m.lengthBeats<=0)
    flag(Severity::Error,"midi_region",regionWhere,"Invalid MIDI clip length or start");
   for(const auto& n:m.notes){
    if(n.pitch<0||n.pitch>127||n.channel<1||n.channel>16||!std::isfinite(n.beat)||n.beat<0||
       !std::isfinite(n.length)||n.length<=0||!std::isfinite(n.velocity)||n.velocity<0||n.velocity>1){
     flag(Severity::Error,"midi_note",regionWhere,"Invalid MIDI note (pitch, velocity, channel, or timing)");break;
    }
   }
   for(const auto& e:m.controllers){
    const bool validType=e.type==MidiControlType::CC||e.type==MidiControlType::PitchWheel||e.type==MidiControlType::ChannelPressure;
    const int maximum=e.type==MidiControlType::PitchWheel?16383:127;
    if(!validType||!std::isfinite(e.beat)||e.beat<0||e.channel<1||e.channel>16||
       e.value<0||e.value>maximum||(e.type==MidiControlType::CC&&(e.number<0||e.number>127))){
     flag(Severity::Error,"midi_controller",regionWhere,"Invalid MIDI controller type, number, value, channel, or time");break;
    }
   }
  }
 }
 return report;
}
}
