#include "SessionHealth.h"
#include "GrooveLab.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
static std::string read(const fs::path& p){std::ifstream file(p,std::ios::binary);check(bool(file),"missing required file");return {(std::istreambuf_iterator<char>(file)),{}};}
int main(){
 const fs::path root=VEYSONA_SOURCE_PATH;
 const auto script=read(root/"publish_to_github.ps1");
 check(script.find("--private")!=std::string::npos,"private GitHub mode missing");
 check(script.find("--public")==std::string::npos,"unsafe public GitHub mode present");
 check(script.find("$visibility -ne 'PRIVATE'")!=std::string::npos,"public repository guard missing");
 check(fs::exists(root/"SOURCE_AND_DISTRIBUTION_POLICY.md"),"missing licence disclosure");
 for(const auto* title:{"House","UK Garage","Breakbeat","Trap"}){
  const auto p=root/"assets/midi"/(std::string("V16 Groove - ")+title+" 120BPM.mid");
  const auto header=read(p);
  check(header.size()>32&&header.substr(0,4)=="MThd"&&header.find("MTrk")!=std::string::npos,"bad V16 MIDI preset");
 }
 check(fs::exists(root/"src/SessionHealth.cpp")&&fs::exists(root/"src/GrooveLab.cpp"),"missing V16 engines");
 std::cout<<"Veysona 1.6 packaging, source privacy and four MIDI presets OK\n";
}
