#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <iostream>
using namespace std;
static string contents(const filesystem::path& f){ifstream in(f);if(!in)throw runtime_error("Missing source file");return {istreambuf_iterator<char>(in),{}};}
static void require(bool value,const char* message){if(!value)throw runtime_error(message);}
int main(){
 auto root=filesystem::path(VEYSONA_SOURCE_PATH);
 auto gui=contents(root/"src/Main.cpp");
 require(gui.find("recordingSpool->push")!=string::npos,"Audio callback lacks disk recording spool");
 require(gui.find("RecordingSpool::readInterleaved")!=string::npos,"Take import not integrated");
 require(gui.find("recordStorage.assign") == string::npos,"Old 60-second recording allocation still present");
 require(gui.find("pluginTrackFaults")!=string::npos,"Plugin-fault UI feedback missing");
 require(gui.find("std::isfinite(scratchL")!=string::npos,"Plugin sample sanitation missing");
 require(gui.find("1.31.0")!=string::npos,"GUI version mismatch");
 require(contents(root/"installer/Veysona.iss").find("1.31.0")!=string::npos,"Installer version mismatch");
 cout<<"PASS: v1.25 source integrations (native Windows GUI not executed)\n";
}
