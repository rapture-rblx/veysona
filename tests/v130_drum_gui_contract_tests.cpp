#include <fstream>
#include <stdexcept>
#include <iostream>
#include <string>
static void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
int main(){
 const std::string base=VEYSONA_SOURCE_PATH;
 std::ifstream file(base+"/src/Main.cpp");
 std::string s((std::istreambuf_iterator<char>(file)),{});
 check(s.find("void PianoRoll::paintDrumGrid")!=std::string::npos,"drum grid paint not implemented");
 check(s.find("if(isDrumTrack()){paintDrumGrid(g);return;}")!=std::string::npos,"drum grid not automatically selected");
 check(s.find("void PianoRoll::drumMouseDown")!=std::string::npos,"drum step editing missing");
 check(s.find("void PianoRoll::drumMouseDrag")!=std::string::npos,"drum step velocity editing missing");
 check(s.find("drumGridSteps=drumGridSteps==16?32:16")!=std::string::npos,"step count toggle missing");
 check(s.find("assignSamplerZones")!=std::string::npos,"multisample loader missing");
 check(s.find("sampleZones\",juce::var(zones)")!=std::string::npos,"multisample project save missing");
 check(s.find("drums\",{}")!=std::string::npos,"drum persistence missing");
 check(s.find("populateDrumKit(tr,\"Recorded Acoustic\")")!=std::string::npos,"GM channel-10 drum detection missing");
 std::cout << "v1.30 GUI integration source contract passed (native JUCE build still required)\n";
}
