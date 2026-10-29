#include "UiGeometry.h"
#include <cmath>
#include <iostream>
#include <cstdlib>
static void require(bool ok, const char* msg) {
 if(!ok){std::cerr << "FAILED: " << msg << '\n';std::exit(1);}
}
int main(){
 using namespace veysona::ui;
 require(measureX(0,180,110.)==180,"first ruler line");
 require(measureX(4,180,110.)==620,"highlighted four-bar line");
 require(subdivisionX(0,2,4,180,110.)==235,"half-bar subdivision");
 require(subdivisionX(1,0,4,180,110.)==measureX(1,180,110.),"measure continuity");
 require(snapBeat(2.19,true)==2.25,"quarter-beat snap");
 require(std::abs(snapBeat(2.19,false)-2.19)<1e-8,"free movement");
 require(snapBeat(-4.,true)==0.,"negative beat clamp");
 require(snapBeat(3.1,true,.5)==3.,"alternative grid step");
 require(fitPixelsPerBar(32.,4,1080)>100.,"eight bar zoom to song");
 require(fitPixelsPerBar(2048.,4,1080)==65.,"long songs zoom floor");
 require(nextFollowScroll(350,0,1000)==0,"no needless follow motion");
 require(nextFollowScroll(1050,0,1000)>0,"follow when playhead exits view");
 require(nextFollowScroll(30,200,1000)==0,"follow when scrubbing back to start");
 std::cout << "Veysona UI geometry passed\n";
}
