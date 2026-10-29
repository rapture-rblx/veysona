#include "SessionHealth.h"
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>
using namespace veysona;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static bool contains(const health::Report& r,const std::string& code){for(const auto& f:r.findings)if(f.code==code)return true;return false;}
int main(){
 Session s;s.tracks.emplace_back();s.tracks[0].midi.emplace_back();
 check(health::inspect(s).errors()==0,"Healthy session erroneously rejected");
 s.tracks[0].midi[0].controllers.push_back({MidiControlType::CC,7,300,1,0});
 auto report=health::inspect(s);check(!report.canExport()&&contains(report,"midi_controller"),"Out-of-range CC not detected");
 s.tracks[0].midi[0].controllers[0].value=127;
 s.tracks[0].automation.push_back({AutomationTarget::TrackGain,true,false,{{2.,1.},{1.,.8f}},false});
 report=health::inspect(s);check(contains(report,"automation_points"),"Unsorted automation not detected");
 s.tracks[0].automation[0].points={{1.,.9f},{2.,.8f}};
 check(health::inspect(s).errors()==0,"Valid ordered automation rejected");
 s.buses.emplace_back();s.buses[0].automation.push_back({AutomationTarget::BusGain,true,false,{{1.,std::numeric_limits<float>::quiet_NaN()}},false});
 check(contains(health::inspect(s),"automation_points"),"Non-finite bus automation not detected");
 std::cout<<"PASS: MIDI CC range and monotonic finite track/bus automation project preflight\n";
}
