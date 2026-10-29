#include "AudioCore.h"
#include <cstdlib>
#define CHECK(expr) do { if(!(expr)){ std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " << #expr << "\n";std::abort(); } } while(0)
#include <iostream>
#include <filesystem>
#include <cmath>
int main(){
 auto dir=std::filesystem::path(VEYSONA_SAMPLES_PATH);
 CHECK(std::filesystem::exists(dir/"Kick - Deep.wav"));
 auto kick=veysona::readWav((dir/"Kick - Deep.wav").string());
 auto snare=veysona::readWav((dir/"Snare - Clean.wav").string());
 auto demo=veysona::readWav((dir/"Loop - Studio Groove 120BPM.wav").string());
 CHECK(kick.stereo.size()>1000 && snare.stereo.size()>1000 && demo.stereo.size()==48000u*16*2);
 int count=0;
 for(const auto& item:std::filesystem::directory_iterator(dir)){
  if(item.path().extension()!=".wav")continue;
  auto sample=veysona::readWav(item.path().string());
  CHECK(sample.stereo.size()>100&&sample.stereo.size()%2==0);
  CHECK(sample.waveformPeaks.size()>0);
  double maxPeak=0;for(const auto peak:sample.waveformPeaks){CHECK(std::isfinite(peak));maxPeak=std::max(maxPeak,double(peak));}
  CHECK(maxPeak>.005&&maxPeak<=1.001);
  ++count;
 }
 CHECK(count>=40);
 veysona::Session session;session.bpm=120;veysona::Track t;t.name="Kick";for(auto beat:{0,2,4,6}){auto c=kick;c.startBeat=beat;t.clips.push_back(c);}session.tracks.push_back(t);
 auto mix=session.render(4.5);CHECK(mix.size()==size_t(48000*9));double power=0;for(auto x:mix)power+=std::abs(x);CHECK(power>1000);
 std::cout<<"Entire sample bank decoded and checked; 4.5s multitrack audio rendered successfully\n";
}
