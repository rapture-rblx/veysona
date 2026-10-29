#pragma once
#include "AudioCore.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace veysona::mix {
// Faders are linear in decibels rather than raw amplitude. The first 2% is
// reserved for an explicit -infinity position; unity sits above mid-travel.
inline float decibels(float gain) {
 if (!(gain > 0.f) || !std::isfinite(gain)) return -100.f;
 return std::clamp(20.f*std::log10(gain),-100.f,12.f);
}
// True log-scale peak meter, -60 to +6 dBFS, with explicit silence.
// The input is a linear peak (not RMS). Readings above 0 dBFS are preserved
// up to +6 dB so overs can be seen instead of disappearing at full scale.
inline float peakMeterFraction(float peak) {
 if(!(peak>0.f)||!std::isfinite(peak))return 0.f;
 const float db=20.f*std::log10(peak);
 return std::clamp((db+60.f)/66.f,0.f,1.f);
}
inline bool peakClipping(float peak) {return std::isfinite(peak)&&peak>=1.f;}
inline float amplitude(float db) {
 if (!std::isfinite(db)||db<=-90.f) return 0.f;
 return std::pow(10.f,std::clamp(db,-90.f,12.f)/20.f);
}
inline float gainFromFader(float fraction) {
 if(!std::isfinite(fraction))return 0.f;
 fraction=std::clamp(fraction,0.f,1.f);
 if(fraction<=.02f)return 0.f;
 const float normalized=(fraction-.02f)/.98f;
 return amplitude(-72.f+78.f*normalized);
}
inline float faderFromGain(float gain) {
 if(!(gain>0.f)||!std::isfinite(gain))return 0.f;
 return .02f+.98f*std::clamp((decibels(gain)+72.f)/78.f,0.f,1.f);
}
inline void setGain(Track& track,float gain) { track.gain=std::clamp(std::isfinite(gain)?gain:0.f,0.f,amplitude(6.f)); }
inline void setPan(Track& track,float pan) { track.pan=std::clamp(std::isfinite(pan)?pan:0.f,-1.f,1.f); }
inline bool setSend(Track& track,size_t index,float level,size_t busCount) {
 if(index>=std::min(busCount,kMaxBuses))return false;
 track.sends[index]=std::clamp(std::isfinite(level)?level:0.f,0.f,1.f);return true;
}
inline bool setOutput(Track& track,int bus,size_t busCount) {
 if(bus < -1||bus>=int(std::min(busCount,kMaxBuses)))return false;
 track.outputBus=bus;return true;
}
inline void toggleSolo(std::vector<Track>& tracks,size_t index,bool exclusive) {
 if(index>=tracks.size())return;
 const bool target=!tracks[index].solo;
 if(exclusive)for(auto& track:tracks)track.solo=false;
 tracks[index].solo=target;
}
// Copy only mix state, never MIDI/audio clips, instruments, sampler content or VST instances.
struct ChannelSettings {
 float gain=1.f,pan=0.f,cutoff=1.f,drive=0.f,delayMix=0.f,stereoWidth=1.f;
 bool mute=false,solo=false;
 int outputBus=-1;
 std::array<float,kMaxBuses> sends{};
 EqSettings eq;CompressorSettings compressor;GateSettings gate;CreativeFxSettings creativeFx;
 RoomReverbSettings room;DeEsserSettings deEsser;TransientSettings transient;
};
inline ChannelSettings copyChannel(const Track& t){
 ChannelSettings c;
 c.gain=t.gain;c.pan=t.pan;c.cutoff=t.cutoff;c.drive=t.drive;c.delayMix=t.delayMix;c.stereoWidth=t.stereoWidth;
 c.mute=t.mute;c.solo=t.solo;c.outputBus=t.outputBus;c.sends=t.sends;
 c.eq=t.eq;c.compressor=t.compressor;c.gate=t.gate;c.creativeFx=t.creativeFx;
 c.room=t.room;c.deEsser=t.deEsser;c.transient=t.transient;return c;
}
inline void pasteChannel(Track& t,const ChannelSettings& c,size_t busCount){
 setGain(t,c.gain);setPan(t,c.pan);t.cutoff=std::clamp(c.cutoff,0.f,1.f);
 t.drive=std::clamp(c.drive,0.f,1.f);t.delayMix=std::clamp(c.delayMix,0.f,1.f);
 t.stereoWidth=std::clamp(c.stereoWidth,0.f,2.f);t.mute=c.mute;t.solo=c.solo;
 t.outputBus=-1;setOutput(t,c.outputBus,busCount);
 for(size_t i=0;i<kMaxBuses;++i)t.sends[i]=i<busCount?std::clamp(c.sends[i],0.f,1.f):0.f;
 t.eq=c.eq;t.compressor=c.compressor;t.gate=c.gate;t.creativeFx=c.creativeFx;
 t.room=c.room;t.deEsser=c.deEsser;t.transient=c.transient;
}
inline bool audible(const Track& track,bool anySolo) {return !track.mute && (!anySolo || track.solo);}
}
