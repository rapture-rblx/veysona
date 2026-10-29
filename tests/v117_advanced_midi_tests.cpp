#include "AdvancedMidi.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace veysona;
using namespace veysona::advanced_midi;
int main(){
 Notes n{{60,0.,.5,.5f},{64,.5,.5,.7f},{67,1.,.5,.9f}};
 gate(n,.5);assert(n[0].length==.25);
 shiftOctaves(n,1);assert(n[0].pitch==72);
 invertPitches(n,72);assert(n[1].pitch==68);
 mirrorTime(n,2.);assert(n[0].beat==1.75);
 setFixedVelocity(n,.3f);assert(n[2].velocity==.3f);
 compressVelocities(n,.5f,0.f);assert(n[0].velocity==.5f);
 scaleDurations(n,2.);assert(n[0].length==.5);
 snapDurations(n,.25);assert(n[0].length==.5);
 trimToRegion(n,2.);assert(n.size()==3);
 Notes dup{{60,0.,1.,.5f},{60,0.,1.,.6f},{64,0.,1.,.5f}};
 removeDuplicates(dup);assert(dup.size()==2);
 limitPolyphony(dup,1);assert(dup.size()==1);
 Notes p{{60,0.,.25,.5f},{62,.5,.25,.5f},{64,1.,.25,.5f}};
 rotatePitches(p,1);assert(p[0].pitch==64);
 accentEvery(p,2,1.5f);assert(p[0].velocity==.75f&&p[1].velocity==.5f);
 makeStaccato(p,.1);assert(p[0].length==.1);
 makeLegato(p,1.);assert(p[0].length==.5);
 stagger(p,.1);assert(p[1].beat>p[0].beat);
 std::cout<<"PASS 16 advanced MIDI transformations\n";
}
