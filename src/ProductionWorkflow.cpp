#include "ProductionWorkflow.h"
#include "MidiTools.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace veysona::workflow {
namespace {
void addTrack(Session& project,const char* name,Instrument instrument,
              int pitch,const std::vector<int>& steps,int bars,double duration,
              float velocity=.78f){
 Track t;t.name=name;t.instrument=instrument;
 t.gain=instrument==Instrument::Noise?.28f:.76f;
 if(instrument==Instrument::Noise){t.synth.attackMs=.2f;t.synth.releaseMs=24.f;t.synth.sustain=.24f;}
 MidiRegion r;r.name=std::string(name)+" Pattern";r.lengthBeats=bars*project.beatsPerBar;
 for(int i=0;i<bars*project.beatsPerBar*2;++i){
  if(steps.empty()||steps[size_t(i)%steps.size()]<0)continue;
  Note n;n.pitch=std::clamp(pitch+steps[size_t(i)%steps.size()],0,127);
  n.beat=i*.5;n.length=duration;n.velocity=velocity;
  r.notes.push_back(n);
 }
 t.midi.push_back(std::move(r));project.tracks.push_back(std::move(t));
}
int pitchFromWindow(const std::vector<float>& mono,size_t begin,int window,int rate){
 const int minLag=std::max(2,int(rate/950.)),maxLag=std::min(window/2,int(rate/75.));
 if(begin+size_t(window+maxLag)>=mono.size())return -1;
 // YIN cumulative mean normalised difference. Strongest first minimum rather
 // than strongest absolute correlation reduces octave errors.
 std::vector<double> delta(size_t(maxLag+1));double running=0;
 for(int tau=1;tau<=maxLag;++tau){
  double sum=0;
  for(int i=0;i<window;++i){const double d=mono[begin+i]-mono[begin+i+tau];sum+=d*d;}
  delta[size_t(tau)]=sum;running+=sum;
  delta[size_t(tau)]=sum*double(tau)/std::max(1.e-15,running);
 }
 int lag=-1;
 for(int tau=minLag;tau<maxLag-1;++tau){
  if(delta[size_t(tau)]<.17){
   while(tau+1<maxLag&&delta[size_t(tau+1)]<delta[size_t(tau)])++tau;
   lag=tau;break;
  }
 }
 if(lag<0){auto it=std::min_element(delta.begin()+minLag,delta.end());
  if(it==delta.end()||*it>.26)return -1;lag=int(it-delta.begin());}
 // Reject unstable unpitched frames.
 const double hz=double(rate)/lag;
 const int midi=int(std::llround(69+12*std::log2(hz/440.)));
 return std::clamp(midi,0,127);
}
}
Session createTemplate(Template type,uint32_t sampleRate){
 if(sampleRate<8000||sampleRate>192000)throw std::invalid_argument("Unsupported project sample rate");
 Session s;s.sampleRate=sampleRate;s.beatsPerBar=4;
 switch(type){
 case Template::House:
  s.bpm=124;
  addTrack(s,"Kick / Percussion",Instrument::Noise,36,{0,-1,-1,-1,0,-1,-1,-1},8,.07,.8f);
  addTrack(s,"Bassline",Instrument::Saw,36,{0,-1,0,7,0,-1,3,-1},8,.36,.72f);
  addTrack(s,"Keys",Instrument::Wavetable,60,{0,-1,-1,7,-1,4,-1,7},8,.42,.65f);
  break;
 case Template::DrumAndBass:
  s.bpm=174;
  addTrack(s,"Breakbeat Kit",Instrument::Noise,38,{0,-1,-1,0,-1,0,-1,-1},8,.055,.76f);
  addTrack(s,"Reese Bass",Instrument::Wavetable,36,{0,-1,0,-1,-1,3,-1,5},8,.3,.82f);
  addTrack(s,"Atmosphere",Instrument::Triangle,60,{0,-1,-1,-1,-1,-1,-1,-1},8,3.5,.48f);
  break;
 case Template::HipHop:
  s.bpm=92;
  addTrack(s,"Drums",Instrument::Noise,36,{0,-1,-1,-1,0,-1,2,-1},8,.065,.75f);
  addTrack(s,"808 Bass",Instrument::Sine,36,{0,-1,0,-1,-1,3,-1,5},8,.46,.95f);
  addTrack(s,"Electric Keys",Instrument::Triangle,60,{0,-1,-1,3,-1,-1,7,-1},8,.7,.55f);
  break;
 case Template::Ambient:
  s.bpm=76;
  addTrack(s,"Slow Pad",Instrument::Wavetable,48,{0,-1,-1,-1,-1,-1,-1,-1},8,3.5,.6f);
  addTrack(s,"Glass Motif",Instrument::Sine,72,{0,-1,-1,7,-1,3,-1,5},8,.65,.52f);
  break;
 }
 for(auto& t:s.tracks){if(t.instrument==Instrument::Wavetable){t.synth.wavetableMorph=.78f;t.synth.releaseMs=560;}}
 return s;
}
MidiRegion transcribeMonophonic(const Clip& clip,double bpm,float sensitivity){
 if(!std::isfinite(bpm)||bpm<20||bpm>400)throw std::invalid_argument("Invalid BPM");
 if(clip.sampleRate<8000||clip.stereo.size()%2||clip.stereo.size()<4096)throw std::invalid_argument("Clip is too short or malformed");
 MidiRegion result;result.name=clip.name+" - Audio to MIDI";
 const uint32_t decimate=std::max(1u,clip.sampleRate/12000u);
 const int reducedRate=int(clip.sampleRate/decimate),window=1024,hop=256;
 std::vector<float> mono;mono.reserve(clip.stereo.size()/(2*decimate)+1);
 for(size_t i=0;i+decimate*2<=clip.stereo.size();i+=2*decimate){
  double avg=0;for(size_t d=0;d<decimate;++d)avg+=(clip.stereo[i+d*2]+clip.stereo[i+d*2+1])*.5;
  mono.push_back(float(avg/decimate));
 }
 struct Frame {int pitch=-1;double at=0;};std::vector<Frame> frames;
 const double frameSeconds=double(hop)/reducedRate;
 float gate=std::clamp(sensitivity,.002f,.7f);
 for(size_t at=0;at+size_t(window+reducedRate/65)<mono.size();at+=hop){
  double energy=0;for(int i=0;i<window;++i)energy+=double(mono[at+i])*mono[at+i];
  const float rms=float(std::sqrt(energy/window));
  const int pitch=rms>=gate?pitchFromWindow(mono,at,window,reducedRate):-1;
  frames.push_back({pitch,(double(at)+window*.5)/reducedRate});
 }
 // Require two matching frames to reject spurious note attacks. Each note
 // remains editable and uses the imported clip's project-position offset.
 for(size_t i=0;i<frames.size();){
  if(frames[i].pitch<0){++i;continue;}
  const int pitch=frames[i].pitch;size_t j=i+1;
  while(j<frames.size()&&frames[j].pitch==pitch)++j;
  if(j-i>=2){
   const double start=std::max(0.,frames[i].at-window*.5/reducedRate);
   const double finish=std::min(double(mono.size())/reducedRate,frames[j-1].at+frameSeconds*.5);
   if(finish-start>=.06){
    Note n;n.pitch=pitch;n.beat=start*bpm/60.;n.length=std::max(.05,(finish-start)*bpm/60.);n.velocity=.78f;
    result.notes.push_back(n);
   }
  }
  i=j;
 }
 result.lengthBeats=std::max(4.,std::ceil(double(clip.stereo.size()/2)/clip.sampleRate*bpm/60./4.)*4.);
 return result;
}
Clip bounceBuiltInTrack(const Session& session,size_t index,double maximumSeconds){
 if(index>=session.tracks.size()||session.sampleRate<8000||session.bpm<=0)
  throw std::invalid_argument("Invalid track or session");
 const auto& tr=session.tracks[index];
 if(!tr.pluginChain.empty()||!tr.pluginPath.empty())
  throw std::invalid_argument("VST3 inserts require host-side bounce");
 if(!std::isfinite(maximumSeconds)||maximumSeconds<=0||maximumSeconds>600)
  throw std::invalid_argument("Invalid bounce duration");
 Session local=session;for(size_t i=0;i<local.tracks.size();++i){local.tracks[i].mute=(i!=index);local.tracks[i].solo=false;}
 local.tracks[index].mute=false;
 double endBeat=0;
 for(const auto& r:tr.midi)endBeat=std::max(endBeat,r.startBeat+r.lengthBeats+1.);
 for(const auto& c:tr.clips)endBeat=std::max(endBeat,c.startBeat+double(c.stereo.size()/2)/c.sampleRate*local.bpm/60.);
 const double seconds=std::min(maximumSeconds,std::max(.25,endBeat*60./session.bpm));
 if(seconds*session.sampleRate>25'000'000)throw std::runtime_error("Bounce exceeds memory limit");
 Clip clip;clip.name=tr.name+" (bounce)";clip.sampleRate=session.sampleRate;clip.startBeat=0;
 const size_t frames=size_t(std::ceil(seconds*session.sampleRate));
 clip.stereo.resize(frames*2);
 EngineState state;state.prepare(local.tracks.size(),local.sampleRate);
 float left[1024]{},right[1024]{};
 for(size_t i=0;i<frames;i+=1024){
  const auto count=int(std::min(size_t(1024),frames-i));
  renderTrackBlock(local,index,int64_t(i),count,left,right,state,true,true);
  for(int j=0;j<count;++j){clip.stereo[(i+j)*2]=left[j];clip.stereo[(i+j)*2+1]=right[j];}
 }
 buildWaveformPeaks(clip);
 return clip;
}
}
