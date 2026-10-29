#include "ProductionAnalysis.h"
#include <cmath>
#include <iostream>
#include <vector>
using namespace veysona::production;
int main(){
 constexpr unsigned sr=48000;
 std::vector<float> mono(sr*2),inverse(sr*2),skew(sr*2),silent(sr*2,0.f);
 for(size_t i=0;i<sr;++i){
  float v=float(.3*std::sin(2*3.141592653589793*440*i/sr));
  mono[i*2]=mono[i*2+1]=v;
  inverse[i*2]=v;inverse[i*2+1]=-v;
  skew[i*2]=v;skew[i*2+1]=v*.5f;
 }
 auto m=analyseLoop(mono,sr),p=analyseLoop(inverse,sr),b=analyseLoop(skew,sr),z=analyseLoop(silent,sr);
 if(m.stereoCorrelation<.999||p.stereoCorrelation>-.999||b.stereoCorrelation<.999||std::abs(b.channelBalanceDb-6.0206)>.03||z.stereoCorrelation!=0||z.channelBalanceDb!=0){
  std::cerr<<"Stereo diagnostics regression failed\n";return 1;
 }
 std::cout<<"PASS stereo phase, polarity, channel balance, silence\n";
}
