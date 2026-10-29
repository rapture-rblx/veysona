#ifdef NDEBUG
#undef NDEBUG // Assertions must be active in Release test executables.
#endif
#include "AudioCore.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
using namespace veysona;

static std::vector<float> sample(const CreativeFxSettings& settings,double bpm=120){
 Session s;s.bpm=bpm;s.sampleRate=48000;s.master.gain=1;s.master.limiterEnabled=false;
 Track t;t.name="tone";t.gain=1;t.cutoff=1;t.eq.enabled=false;t.compressor.enabled=false;
 t.creativeFx=settings;
 Clip c;c.name="source";c.sampleRate=48000;c.stereo.resize(48000*2);
 for(size_t i=0;i<48000;++i){const float v=.31f*std::sin(float(2*kPi*440*i/48000.));c.stereo[2*i]=v;c.stereo[2*i+1]=v*.8f;}
 t.clips.push_back(std::move(c));s.tracks.push_back(std::move(t));
 return s.render(1.);
}
static double difference(const std::vector<float>& a,const std::vector<float>& b){
 assert(a.size()==b.size());double error=0;
 for(size_t i=0;i<a.size();++i){assert(std::isfinite(a[i])&&std::isfinite(b[i]));error+=std::abs(double(a[i])-b[i]);}
 return error/a.size();
}
int main(){
 const auto dry=sample({});
 CreativeFxSettings tremolo; tremolo.tremoloEnabled=true;tremolo.tremoloDepth=.91f;tremolo.tremoloPeriodBeats=.5f;
 assert(difference(dry,sample(tremolo))>.025);
 CreativeFxSettings ring;ring.ringModEnabled=true;ring.ringModFrequencyHz=100.f;ring.ringModMix=.85f;
 assert(difference(dry,sample(ring))>.04);
 CreativeFxSettings tape;tape.tapeEnabled=true;tape.tapeDrive=10.f;tape.tapeMix=.85f;
 assert(difference(dry,sample(tape))>.015);
 CreativeFxSettings all;tape.tapeEnabled=false;
 all.tapeEnabled=true;all.tapeDrive=50;all.tapeMix=10;
 all.tremoloEnabled=true;all.tremoloDepth=20;all.tremoloPeriodBeats=0;
 all.ringModEnabled=true;all.ringModFrequencyHz=100000;all.ringModMix=10;
 auto distorted=sample(all,85.);
 for(const auto v:distorted)assert(std::isfinite(v));
 const auto expanded=std::filesystem::path(VEYSONA_SOURCE_PATH)/"assets";
 size_t samples=0, instruments=0;
 for(const auto& e:std::filesystem::directory_iterator(expanded/"samples")){
  if(e.path().extension()!=".wav")continue;
  const auto clip=readWav(e.path().string());assert(clip.sampleRate==48000 && !clip.stereo.empty());++samples;
 }
 for(const auto& e:std::filesystem::directory_iterator(expanded/"instruments")){
  if(e.path().extension()!=".wav")continue;
  const auto clip=readWav(e.path().string());assert(clip.sampleRate==48000 && !clip.stereo.empty());++instruments;
 }
 assert(samples>=81 && instruments>=28);
 assert(std::filesystem::exists(expanded/"branding"/"Veysona-Logo.png"));
 assert(std::filesystem::exists(expanded/"branding"/"Veysona-Icon.png"));
 std::cout<<"Three new v1.1 effects, extreme parameter clamps, "<<samples<<" WAV samples and "
          <<instruments<<" instrument presets passed\n";
}
