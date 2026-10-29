#include "MidiTools.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
namespace veysona::midi {
double snap(double beat,double grid){return grid>0 && std::isfinite(grid)?std::round(std::max(0.,beat)/grid)*grid:std::max(0.,beat);}
void sanitize(std::vector<Note>& notes,double maxBeats){
 for(auto& n:notes){n.pitch=std::clamp(n.pitch,0,127);n.beat=std::clamp(std::isfinite(n.beat)?n.beat:0.,0.,maxBeats);n.length=std::clamp(std::isfinite(n.length)?n.length:.25,.015625,std::max(.015625,maxBeats-n.beat));n.velocity=std::clamp(std::isfinite(n.velocity)?n.velocity:.8f,.01f,1.f);}
 std::stable_sort(notes.begin(),notes.end(),[](const Note& a,const Note& b){return a.beat<b.beat||(a.beat==b.beat&&a.pitch<b.pitch);});
}
void quantize(std::vector<Note>& notes,double grid,bool ends){if(!(grid>0))return;for(auto& n:notes){auto end=n.beat+n.length;n.beat=snap(n.beat,grid);n.length=ends?std::max(grid,snap(end,grid)-n.beat):n.length;}sanitize(notes);}
void quantizeStrength(std::vector<Note>& notes,double grid,float strength){
 if(!(grid>0)||!std::isfinite(grid)||!std::isfinite(strength))return;
 const double amount=std::clamp(double(strength),0.,1.);
 for(auto& note:notes)note.beat=std::max(0.,note.beat+(snap(note.beat,grid)-note.beat)*amount);
 sanitize(notes);
}
int nearestScalePitch(int pitch,int rootPitchClass,bool minor){
 const auto major=std::array<int,7>{0,2,4,5,7,9,11};
 const auto naturalMinor=std::array<int,7>{0,2,3,5,7,8,10};
 const auto& scale=minor?naturalMinor:major;
 rootPitchClass=(rootPitchClass%12+12)%12;
 pitch=std::clamp(pitch,0,127);
 for(int offset=0;offset<=127;++offset){
  for(int sign:{0,1}){
   const int candidate=pitch+(sign==0?-offset:offset);
   if(candidate<0||candidate>127)continue;
   const int relative=(candidate-rootPitchClass+132)%12;
   if(std::find(scale.begin(),scale.end(),relative)!=scale.end())return candidate;
  }
 }
 return pitch;
}
void constrainToNamedScale(std::vector<Note>& notes,int rootPitchClass,bool minor){
 for(auto& n:notes)n.pitch=nearestScalePitch(n.pitch,rootPitchClass,minor);
 sanitize(notes);
}
void velocityCurve(std::vector<Note>& notes,float exponent,float amount){
 if(!std::isfinite(exponent)||!std::isfinite(amount))return;
 exponent=std::clamp(exponent,.1f,8.f);amount=std::clamp(amount,0.f,2.f);
 for(auto& n:notes)n.velocity=std::clamp(std::pow(std::clamp(n.velocity,0.f,1.f),exponent)*amount,.01f,1.f);
}
void transpose(std::vector<Note>& notes,int semi){for(auto& n:notes)n.pitch=std::clamp(n.pitch+semi,0,127);}
void shift(std::vector<Note>& notes,double beats){double left=0;for(const auto& n:notes)left=std::min(left,n.beat+beats);for(auto& n:notes)n.beat=std::max(0.,n.beat+beats-left);sanitize(notes);}
void duplicate(std::vector<Note>& notes,double offset){if(offset<=0||notes.size()>10000)return;const auto size=notes.size();notes.reserve(size*2);for(size_t i=0;i<size;++i){auto copy=notes[i];copy.beat+=offset;notes.push_back(copy);}sanitize(notes);}
void legato(std::vector<Note>& notes,double end){sanitize(notes);for(size_t i=0;i<notes.size();++i){double next=end;for(size_t j=i+1;j<notes.size();++j)if(notes[j].pitch==notes[i].pitch && notes[j].beat>notes[i].beat){next=notes[j].beat;break;}notes[i].length=std::max(.015625,next-notes[i].beat);}}
void scaleVelocity(std::vector<Note>& notes,float factor){for(auto& n:notes)n.velocity=std::clamp(n.velocity*factor,.01f,1.f);}
void reverse(std::vector<Note>& notes,double clipLengthBeats){
 for(auto& n:notes)n.beat=std::max(0.,clipLengthBeats-n.beat-n.length);
 sanitize(notes);
}
void constrainToScale(std::vector<Note>& notes,int rootPitchClass,const std::vector<int>& intervals){
 if(intervals.empty())return;rootPitchClass=(rootPitchClass%12+12)%12;
 for(auto& n:notes){int best=n.pitch,dist=128;
  for(int p=0;p<128;++p){int pc=(p-rootPitchClass+120)%12;
   if(std::find(intervals.begin(),intervals.end(),pc)!=intervals.end()){
    int d=std::abs(p-n.pitch);if(d<dist){best=p;dist=d;}
   }
  }n.pitch=best;
 }
 sanitize(notes);
}
void makeTriads(std::vector<Note>& notes,bool minor){
 if(notes.size()>3000)return;
 const auto original=notes;
 for(auto n:original){n.pitch=std::clamp(n.pitch+(minor?3:4),0,127);notes.push_back(n);}
 for(auto n:original){n.pitch=std::clamp(n.pitch+7,0,127);notes.push_back(n);}
 sanitize(notes);
}
void applySwing(std::vector<Note>& notes,double gridBeats,double swingPercent){
 if(!(gridBeats>0)||!std::isfinite(gridBeats))return;
 const double shift=gridBeats*std::clamp(swingPercent,0.,100.)/100.*.5;
 for(auto& n:notes){auto gridSlot=int64_t(std::llround(n.beat/gridBeats));
  if((gridSlot&1)!=0 && std::abs(n.beat-gridSlot*gridBeats)<gridBeats*.06)n.beat+=shift;
 }
 sanitize(notes);
}
std::vector<Note> makeEuclideanRhythm(int steps,int hits,int pitch,double grid,int rotation,float velocity){
 if(steps<1||steps>128||hits<0||hits>steps||grid<=0||grid>16||!std::isfinite(grid))
  throw std::invalid_argument("Invalid Euclidean pattern settings");
 std::vector<Note> result;
 // The difference of two floor functions distributes hits evenly among steps.
 for(int step=0;step<steps;++step){
  const auto rotated=(step+((rotation%steps)+steps)%steps)%steps;
  const bool on=(int64_t(rotated+1)*hits)/steps>(int64_t(rotated)*hits)/steps;
  if(on)result.push_back({std::clamp(pitch,0,127),step*grid,grid*.75f,std::clamp(velocity,.01f,1.f),10});
 }
 return result;
}
void strumChords(std::vector<Note>& notes,double spread,bool ascending){
 if(spread<0||spread>.25||!std::isfinite(spread)||notes.size()>10000)throw std::invalid_argument("Invalid strum");
 // Group by onsets before altering them to avoid chasing altered note positions.
 std::stable_sort(notes.begin(),notes.end(),[](const auto& a,const auto& b){return a.beat<b.beat;});
 for(size_t begin=0;begin<notes.size();){
  size_t end=begin+1;
  while(end<notes.size()&&std::abs(notes[end].beat-notes[begin].beat)<.00001)++end;
  std::stable_sort(notes.begin()+std::ptrdiff_t(begin),notes.begin()+std::ptrdiff_t(end),
   [ascending](auto a,auto b){return ascending?a.pitch<b.pitch:a.pitch>b.pitch;});
  const double onset=notes[begin].beat;
  for(size_t i=begin;i<end;++i)notes[i].beat=onset+(i-begin)*spread;
  begin=end;
 }
 sanitize(notes);
}
void repeatNotes(std::vector<Note>& notes,int repeats){
 if(repeats<1||repeats>16||notes.size()>8192/size_t(repeats))throw std::invalid_argument("Invalid note-repeat count");
 std::vector<Note> out;out.reserve(notes.size()*size_t(repeats));
 for(const auto& n:notes){
  if(!std::isfinite(n.length)||n.length<=0)continue;
  const auto step=n.length/repeats;
  for(int i=0;i<repeats;++i){auto v=n;v.beat+=i*step;v.length=step*.9;out.push_back(v);}
 }
 notes=std::move(out);sanitize(notes);
}
void applyProbability(std::vector<Note>& notes,float probability,uint32_t seed){
 if(!std::isfinite(probability)||probability<0||probability>1)throw std::invalid_argument("Invalid probability");
 std::mt19937 rng(seed);
 std::bernoulli_distribution keep(probability);
 notes.erase(std::remove_if(notes.begin(),notes.end(),[&](const Note&){return !keep(rng);}),notes.end());
}
void velocityRamp(std::vector<Note>& notes,float from,float to){
 if(!std::isfinite(from)||!std::isfinite(to))throw std::invalid_argument("Invalid velocity values");
 if(notes.empty())return;
 auto range=std::minmax_element(notes.begin(),notes.end(),[](auto a,auto b){return a.beat<b.beat;});
 const auto first=range.first->beat,last=range.second->beat;
 for(auto& n:notes){const float p=last>first?float((n.beat-first)/(last-first)):0.f;
  n.velocity=std::clamp(from+(to-from)*p,.01f,1.f);}
}
void applyGroove(std::vector<Note>& notes,double grid,const std::vector<double>& offsets){
 if(!(grid>0)||grid>16||!std::isfinite(grid)||offsets.empty()||offsets.size()>128)
  throw std::invalid_argument("Invalid groove template");
 for(double v:offsets)if(!std::isfinite(v)||std::abs(v)>.49)throw std::invalid_argument("Invalid groove offset");
 for(auto& n:notes){
  const auto tick=std::llround(n.beat/grid);
  if(tick<0)continue;
  if(std::abs(n.beat-tick*grid)<grid*.05)
   n.beat=std::max(0.,n.beat+offsets[size_t(tick)%offsets.size()]*grid);
 }
 sanitize(notes);
}
void humanize(std::vector<Note>& notes,double maxJitter,float velVar,uint32_t seed){std::mt19937 rng(seed);std::uniform_real_distribution<double> jitter(-std::max(0.,maxJitter),std::max(0.,maxJitter));std::uniform_real_distribution<float> vel(-std::max(0.f,velVar),std::max(0.f,velVar));for(auto& n:notes){n.beat=std::max(0.,n.beat+jitter(rng));n.velocity=std::clamp(n.velocity+vel(rng),.01f,1.f);}sanitize(notes);}
void quantizeControllers(std::vector<MidiControllerEvent>& controls,double gridBeats){
 if(gridBeats<=0)return;
 for(auto& e:controls)e.beat=std::max(0.,snap(e.beat,gridBeats));
 std::stable_sort(controls.begin(),controls.end(),[](auto a,auto b){return a.beat<b.beat;});
}
void addArpeggio(std::vector<Note>& notes,double stepBeats,int octaves,bool ascending){
 if(notes.empty()||stepBeats<1./128.||stepBeats>4.||octaves<1||octaves>4)return;
 std::vector<int> pitches;
 for(const auto& n:notes)pitches.push_back(std::clamp(n.pitch,0,127));
 std::sort(pitches.begin(),pitches.end());pitches.erase(std::unique(pitches.begin(),pitches.end()),pitches.end());
 if(pitches.size()>64)return;
 auto start=std::min_element(notes.begin(),notes.end(),[](auto a,auto b){return a.beat<b.beat;})->beat;
 std::vector<Note> generated;
 for(int octave=0;octave<octaves;++octave){
  for(size_t i=0;i<pitches.size();++i){
   const auto index=ascending?i:pitches.size()-1-i;
   auto pitch=std::min(127,pitches[index]+12*octave);
   generated.push_back({pitch,start+stepBeats*generated.size(),stepBeats*.95f,.78f});
  }
 }
 notes=std::move(generated);
}
void addChordInversions(std::vector<Note>& notes,int inversion){
 if(notes.size()<2||std::abs(inversion)>3)return;
 std::stable_sort(notes.begin(),notes.end(),[](auto a,auto b){return a.pitch<b.pitch;});
 for(int step=0;step<std::abs(inversion);++step){
  if(inversion>0){notes[step%notes.size()].pitch=std::min(127,notes[step%notes.size()].pitch+12);}
  else{notes[notes.size()-1-(step%notes.size())].pitch=std::max(0,notes[notes.size()-1-(step%notes.size())].pitch-12);}
 }
 std::stable_sort(notes.begin(),notes.end(),[](auto a,auto b){return a.pitch<b.pitch;});
}
void Recorder::noteOn(int pitch,int channel,double beat,float velocity){
 if(!std::isfinite(beat)||beat<0||held.size()>=256)return;
 held.push_back({std::clamp(pitch,0,127),std::clamp(channel,1,16),beat,std::clamp(velocity,.01f,1.f)});
}
void Recorder::noteOff(int pitch,int channel,double beat,std::vector<Note>& destination){
 if(!std::isfinite(beat))return;
 auto it=std::find_if(held.begin(),held.end(),[&](auto n){return n.pitch==pitch&&n.channel==channel;});
 if(it==held.end())return;
 destination.push_back({it->pitch,it->beat,std::max(.015625,beat-it->beat),it->velocity,it->channel});
 held.erase(it);
}
void Recorder::flush(double beat,std::vector<Note>& destination){
 for(const auto& n:held)destination.push_back({n.pitch,n.beat,std::max(.015625,beat-n.beat),n.velocity,n.channel});
 held.clear();
}
}

namespace veysona::midi {
KeyEstimate estimateKey(const std::vector<Note>& notes){
 static constexpr double major[]={6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88};
 static constexpr double minor[]={6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17};
 std::array<double,12> count{};double weight=0;
 for(const auto& n:notes){
  if(n.pitch<0||n.pitch>127||!std::isfinite(n.length)||!std::isfinite(n.velocity))continue;
  auto w=std::clamp(n.length,0.,8.)*std::clamp(double(n.velocity),0.,1.);
  count[size_t(n.pitch%12)]+=w;weight+=w;
 }
 if(weight<.00001)return {};
 const auto score=[&](const double* profile,int root){
  double sum=0,profileMean=0,mean=0;
  for(int i=0;i<12;++i){sum+=count[size_t((i+root)%12)]*profile[i];profileMean+=profile[i];mean+=count[size_t(i)];}
  mean/=12;profileMean/=12;
  double denA=0,denB=0;
  for(int i=0;i<12;++i){denA+=std::pow(count[size_t(i)]-mean,2);denB+=std::pow(profile[i]-profileMean,2);}
  return (sum-12*mean*profileMean)/std::sqrt(std::max(denA*denB,1e-10));
 };
 double best=-10,second=-10;KeyEstimate result;
 for(int root=0;root<12;++root)for(int min=0;min<2;++min){
  const double x=score(min?minor:major,root);
  if(x>best){second=best;best=x;result.root=root;result.minor=bool(min);}
  else if(x>second)second=x;
 }
 result.confidence=std::clamp((best-second+.04)*2.,0.,1.);
 return result;
}
std::vector<Note> chordProgression(int rootMidi,const std::vector<int>& degrees,bool minor,double beatsPerChord,float velocity){
 if(rootMidi<24||rootMidi>100||degrees.size()>128||beatsPerChord<.25||beatsPerChord>16||!std::isfinite(beatsPerChord))
  throw std::invalid_argument("Invalid chord progression");
 const std::array<int,7> intervals=minor?std::array<int,7>{0,2,3,5,7,8,10}:
                                          std::array<int,7>{0,2,4,5,7,9,11};
 std::vector<Note> notes;notes.reserve(degrees.size()*3);
 for(size_t i=0;i<degrees.size();++i){
  auto d=degrees[i];if(d<1||d>7)throw std::invalid_argument("Chord degrees must be 1..7");
  const int di=d-1;
  const int intervalsFromRoot[]={0,intervals[(di+2)%7]+((di+2)>=7?12:0)-intervals[di],
                                 intervals[(di+4)%7]+((di+4)>=7?12:0)-intervals[di]};
  for(auto semitone:intervalsFromRoot){
   const int p=rootMidi+intervals[di]+semitone;
   notes.push_back({std::clamp(p,0,127),i*beatsPerChord,beatsPerChord*.94f,std::clamp(velocity,.01f,1.f),1});
  }
 }
 return notes;
}
std::vector<Note> basslineFromChords(const std::vector<Note>& chords,double stepBeats,uint32_t seed){
 if(stepBeats<.125||stepBeats>4||!std::isfinite(stepBeats))throw std::invalid_argument("Invalid bass step");
 if(chords.empty())return {};
 std::vector<Note> roots;
 for(const auto& n:chords){
  if(!std::isfinite(n.beat)||!std::isfinite(n.length))continue;
  auto it=std::find_if(roots.begin(),roots.end(),[&](const Note& r){return std::abs(r.beat-n.beat)<.001;});
  if(it==roots.end())roots.push_back(n);
  else if(n.pitch<it->pitch)it->pitch=n.pitch;
 }
 std::stable_sort(roots.begin(),roots.end(),[](auto a,auto b){return a.beat<b.beat;});
 std::mt19937 rng(seed);std::bernoulli_distribution octaveHop(.19),skip(.08);
 std::vector<Note> bass;
 for(size_t k=0;k<roots.size();++k){
  const auto& root=roots[k];double end=k+1<roots.size()?roots[k+1].beat:root.beat+4.;
  end=std::min(end,root.beat+16.);
  for(double beat=root.beat;beat+stepBeats*.5<end;beat+=stepBeats){
   if(skip(rng))continue;
   auto pitch=root.pitch-24+(octaveHop(rng)?12:0);
   bass.push_back({std::clamp(pitch,0,127),beat,stepBeats*.84f,.63f,1});
  }
 }
 return bass;
}
std::vector<Note> drumStepPattern(int bars,int beatsPerBar,uint32_t seed,double complexity){
 if(bars<1||bars>64||beatsPerBar<2||beatsPerBar>12||!std::isfinite(complexity))
  throw std::invalid_argument("Invalid step pattern settings");
 complexity=std::clamp(complexity,0.,1.);
 std::mt19937 rng(seed);std::uniform_real_distribution<double> random(0.,1.);
 std::vector<Note> notes;
 for(int step=0;step<bars*beatsPerBar*4;++step){
  const int quarter=step%4;const int beat=(step/4)%beatsPerBar;
  const auto at=step*.25;
  if(quarter==0 && (beat==0||random(rng)<complexity*.35))notes.push_back({36,at,.20,.9f,10});
  if(quarter==0 && beat%2==1)notes.push_back({38,at,.20,.8f,10});
  if(step%2==0 || random(rng)<complexity*.37)notes.push_back({42,at,.12f,.42f+float(random(rng)*.27),10});
  if(random(rng)<complexity*.022)notes.push_back({46,at,.26f,.55f,10});
 }
 return notes;
}
}
