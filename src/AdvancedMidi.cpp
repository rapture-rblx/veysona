#include "AdvancedMidi.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace veysona::advanced_midi {
namespace {
double safe(double v,double fallback=0.) {return std::isfinite(v)?v:fallback;}
int pitch(int p){return std::clamp(p,0,127);}
float velocity(float v){return std::clamp(std::isfinite(v)?v:.8f,0.f,1.f);}
}
void gate(Notes& n,double f){f=std::clamp(safe(f,1.),.01,16.);for(auto& x:n)x.length=std::max(.001,safe(x.length,.25)*f);}
void shiftOctaves(Notes& n,int o){o=std::clamp(o,-10,10);for(auto& x:n)x.pitch=pitch(x.pitch+o*12);}
void invertPitches(Notes& n,int pivot){pivot=pitch(pivot);for(auto& x:n)x.pitch=pitch(2*pivot-x.pitch);}
void mirrorTime(Notes& n,double region){region=std::max(0.,safe(region));for(auto& x:n)x.beat=std::max(0.,region-safe(x.beat)-std::max(0.,safe(x.length)));}
void removeDuplicates(Notes& n,double tol){tol=std::max(0.,safe(tol));std::stable_sort(n.begin(),n.end(),[](auto&a,auto&b){return a.beat<b.beat;});Notes out;for(const auto& x:n){bool dup=false;for(const auto& y:out)if(x.pitch==y.pitch&&x.channel==y.channel&&std::abs(x.beat-y.beat)<=tol&&std::abs(x.length-y.length)<=tol){dup=true;break;}if(!dup)out.push_back(x);}n=std::move(out);}
void limitPolyphony(Notes& n,int voices){voices=std::clamp(voices,1,128);std::stable_sort(n.begin(),n.end(),[](auto&a,auto&b){return a.beat<b.beat;});Notes out;for(const auto& x:n){int overlapping=0;for(const auto& y:out)if(y.beat<=x.beat&&y.beat+y.length>x.beat)++overlapping;if(overlapping<voices)out.push_back(x);}n=std::move(out);}
void setFixedVelocity(Notes& n,float v){v=velocity(v);for(auto& x:n)x.velocity=v;}
void compressVelocities(Notes& n,float centre,float ratio){centre=velocity(centre);ratio=std::clamp(std::isfinite(ratio)?ratio:1.f,0.f,8.f);for(auto& x:n)x.velocity=velocity(centre+(velocity(x.velocity)-centre)*ratio);}
void scaleDurations(Notes& n,double factor,double minimum){factor=std::clamp(safe(factor,1.),.01,32.);minimum=std::max(.001,safe(minimum,.01));for(auto& x:n)x.length=std::max(minimum,safe(x.length,.25)*factor);}
void stagger(Notes& n,double interval){interval=std::clamp(safe(interval),-64.,64.);std::stable_sort(n.begin(),n.end(),[](auto&a,auto&b){return a.pitch<b.pitch;});for(size_t i=0;i<n.size();++i)n[i].beat=std::max(0.,safe(n[i].beat)+interval*double(i));}
void rotatePitches(Notes& n,int positions){if(n.empty())return;positions%=int(n.size());if(positions<0)positions+=int(n.size());std::vector<int> p;for(auto& x:n)p.push_back(x.pitch);std::rotate(p.rbegin(),p.rbegin()+positions,p.rend());for(size_t i=0;i<n.size();++i)n[i].pitch=p[i];}
void trimToRegion(Notes& n,double length){length=std::max(0.,safe(length));n.erase(std::remove_if(n.begin(),n.end(),[&](const Note& x){return !std::isfinite(x.beat)||x.beat>=length||x.beat<0.;}),n.end());for(auto& x:n)x.length=std::max(.001,std::min(safe(x.length,.25),length-x.beat));}
void accentEvery(Notes& n,int interval,float boost){interval=std::max(1,interval);boost=std::clamp(std::isfinite(boost)?boost:1.f,0.f,8.f);std::stable_sort(n.begin(),n.end(),[](auto&a,auto&b){return a.beat<b.beat;});for(size_t i=0;i<n.size();++i)if(i%interval==0)n[i].velocity=velocity(n[i].velocity*boost);}
void snapDurations(Notes& n,double grid){grid=std::max(.001,safe(grid,.25));for(auto& x:n)x.length=std::max(grid,std::round(safe(x.length,grid)/grid)*grid);}
void makeStaccato(Notes& n,double maxDuration){maxDuration=std::max(.001,safe(maxDuration,.125));for(auto& x:n)x.length=std::min(std::max(.001,safe(x.length,.25)),maxDuration);}
void makeLegato(Notes& n,double maxLength){maxLength=std::max(.001,safe(maxLength,4.));std::stable_sort(n.begin(),n.end(),[](auto&a,auto&b){return a.beat<b.beat;});for(size_t i=0;i+1<n.size();++i)if(n[i+1].beat>n[i].beat)n[i].length=std::min(maxLength,n[i+1].beat-n[i].beat);}
void makePolyphonicLegato(Notes& n,double maximum){
 maximum=std::clamp(safe(maximum,8.),.001,128.);
 std::stable_sort(n.begin(),n.end(),[](const Note& a,const Note& b){return a.beat<b.beat;});
 for(size_t i=0;i<n.size();++i){
  if(!std::isfinite(n[i].beat)||n[i].beat<0.)continue;
  const double start=n[i].beat;
  double end=start+maximum;
  for(size_t j=i+1;j<n.size();++j){
   if(n[j].pitch==n[i].pitch&&n[j].channel==n[i].channel&&n[j].beat>start){end=std::min(end,n[j].beat);break;}
  }
  n[i].length=std::max(.001,end-start);
 }
}
void resolveSameVoiceOverlaps(Notes& n,double minimum){
 minimum=std::clamp(safe(minimum,.001),.001,1.);
 std::stable_sort(n.begin(),n.end(),[](const Note& a,const Note& b){return a.beat<b.beat;});
 for(size_t i=0;i<n.size();++i){
  const double start=safe(n[i].beat),length=std::max(minimum,safe(n[i].length,minimum));
  for(size_t j=i+1;j<n.size();++j){
   if(n[j].pitch==n[i].pitch&&n[j].channel==n[i].channel&&n[j].beat>start){
    n[i].length=std::max(minimum,std::min(length,n[j].beat-start));break;
   }
  }
 }
}

}
