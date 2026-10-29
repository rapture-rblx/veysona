#include "GrooveLab.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace veysona::groove {
std::vector<Note> generate(const Settings& cfg) {
 const int bars=std::clamp(cfg.bars,1,16);
 const int steps=std::clamp(cfg.stepsPerBar,8,32);
 const double density=std::clamp(cfg.density,0.,1.);
 const double swing=std::clamp(cfg.swing,0.,.45);
 const double spacing=4./double(steps);
 std::mt19937 rng(cfg.seed);
 std::uniform_real_distribution<double> chance(0.,1.);
 std::vector<Note> out;
 out.reserve(size_t(bars*steps)*3);
 auto hit=[&](int pitch,int bar,int step,float velocity,double duration=0.18){
  const double at=double(bar)*4.+double(step)*spacing + ((step%2)?swing*spacing:0.);
  out.push_back({pitch,at,std::min(spacing*.9,duration),std::clamp(velocity,.01f,1.f),10});
 };
 for(int b=0;b<bars;++b){
  for(int s=0;s<steps;++s){
   const double q=double(s)*4/double(steps);
   const bool beat=std::abs(q-std::round(q))<1e-6;
   const bool backbeat=std::abs(q-1.)<1e-6||std::abs(q-3.)<1e-6;
   const bool halfbeat=std::abs(q*2-std::round(q*2))<1e-6;
   bool kick=false,snare=false;
   switch(cfg.style){
    case Style::House:kick=beat;snare=backbeat;break;
    case Style::Garage:kick=(s==0||s==int(std::round(2.5/spacing)));snare=backbeat;break;
    case Style::Breakbeat:kick=(s==0||s==int(std::round(1.5/spacing))||s==int(std::round(2.75/spacing)));
     snare=backbeat;break;
    case Style::Trap:kick=(s==0||s==int(std::round(2.75/spacing)));snare=std::abs(q-2.)<1e-6;break;
   }
   if(kick)hit(36,b,s,beat?.95f:.81f);
   if(snare)hit(38,b,s,.90f);
   if(!kick&&!snare&&chance(rng)<density*.12 && s>0)hit(36,b,s,.42f);
   if(!snare && chance(rng)<density*.10 && s>0)hit(38,b,s,.29f);
   if(halfbeat || chance(rng)<density*.45){
    const float accent=(beat?.73f:(halfbeat?.55f:.37f));
    hit(42,b,s,std::clamp(accent+float(chance(rng)*.09),.05f,1.f),.09);
   }
   if(s==steps-1 && b%4==3 && chance(rng)<density*.9)hit(46,b,s,.7f,.12);
  }
 }
 std::stable_sort(out.begin(),out.end(),[](const Note& a,const Note& b){return a.beat<b.beat;});
 return out;
}
}
