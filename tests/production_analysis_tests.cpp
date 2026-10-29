#include "ProductionAnalysis.h"
#include "MidiTools.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace veysona;
using namespace veysona::production;
static void check(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
static std::vector<float> beatDemo(uint32_t sr,double bpm,int bars=4){
 const size_t frames=size_t(std::llround(bars*4*60./bpm*sr));
 std::vector<float> a(frames*2);
 for(int beat=0;beat<bars*4;++beat){
  const auto at=size_t(std::llround(beat*60./bpm*sr));
  for(size_t j=0;j<sr/40&&at+j<frames;++j){
   const double decay=std::exp(-double(j)/(sr*.002));
   const float value=float(.82*std::sin(2*3.141592653589793*60*j/sr)*decay+.45*decay);
   a[(at+j)*2]=value;a[(at+j)*2+1]=value;
  }
 }
 return a;
}
int main(){
 const uint32_t sr=48000;
 auto drum=beatDemo(sr,120);
 auto report=analyseLoop(drum,sr,90,150);
 std::cout<<"Detected "<<report.estimatedBpm<<" bpm, transients "<<report.transientsSeconds.size()<<" conf "<<report.confidence<<"\n";
 check(report.estimatedBpm>116&&report.estimatedBpm<124,"tempo estimation wrong");
 check(report.transientsSeconds.size()>=12,"expected many detected transients");
 check(report.approximateLufs>-60&&report.approximateLufs<0,"loudness unreasonable");
 check(report.estimatedTruePeakDbfs>=report.peakDbfs-.15,"true peak smaller than sample peak");
 auto down=resampleSinc(drum,sr,24000);
 check(std::abs(double(down.size())/drum.size()-.5)<.001,"resampler output count");
 auto back=resampleSinc(down,24000,sr);
 check(back.size()==drum.size(),"roundtrip output count");
 check(*std::max_element(back.begin(),back.end())>.35f,"resampler too quiet");
 auto silent=std::vector<float>(48000*2,0.f);
 check(analyseLoop(silent,sr).estimatedBpm==0,"silence detected tempo");
 check(integratedLoudnessApprox(silent,sr)<=-110.,"silence loudness");
 std::vector<float> mastered=drum;
 const auto master=normalizeLoudnessOffline(mastered,sr,-17.,-.5);
 check(std::isfinite(master.outputLufs)&&master.outputTruePeak<=-.3,"master ceiling");
 check(mastered.size()==drum.size(),"master changed audio length");
 Clip c;c.name="Beat loop";c.sampleRate=sr;c.stereo=drum;c.startBeat=8;
 auto slices=sliceClipAtTransients(c,.7,.15,40,120);
 check(slices.size()>5,"transient slicing");
 check(std::abs(slices.front().startBeat-8.)<.00001,"slice start offset");
 size_t total=0;for(const auto& slice:slices){check(!slice.stereo.empty(),"empty slice");total+=slice.stereo.size();}
 check(total==drum.size(),"slicing must preserve original samples");
 auto chords=midi::chordProgression(60,{1,5,6,4},false,4.);
 check(chords.size()==12&&chords[0].pitch==60&&chords[1].pitch==64&&chords[2].pitch==67,"chord generator");
 auto bass=midi::basslineFromChords(chords,.5,34);
 check(bass.size()>=24&&bass.size()<=32,"bassline generator");
 auto beat=midi::drumStepPattern(2,4,55,.6);
 check(beat.size()>10,"step drum generator");
 auto key=midi::estimateKey(chords);
 check(key.root>=0&&key.root<12,"key estimator");
 std::cout<<"Production Analysis v1.4: all checks passed\n";
}
