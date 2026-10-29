// Portable source-integration check. The JUCE audio callback and physical VST3
// hosting are only validated by a native Windows build and DAW smoke test.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
namespace fs=std::filesystem;
static void check(bool test,const char* message){if(!test)throw std::runtime_error(message);}
static std::string contents(const fs::path& f){std::ifstream in(f);check(bool(in),"source file absent");return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 const fs::path root(VEYSONA_SOURCE_PATH);
 const auto source=contents(root/"src/Main.cpp");
 check(source.find("juce::MidiMessageCollector livePluginMidi")!=std::string::npos,"monitor queue missing");
 check(source.find("owner.livePluginMidi.addMessageToQueue(msg)")!=std::string::npos,"hardware MIDI not routed to monitor");
 check(source.find("livePluginMidi.removeNextBlockOfMessages(liveInput,frames)")!=std::string::npos,"audio callback not draining MIDI");
 check(source.find("if(!playing||outputCh<1||!outputs[0]){")!=std::string::npos,"stopped-transport branch absent");
 check(source.find("if(synth){\n    for(int pos=0;pos<frames;)")!=std::string::npos,"stopped-transport synth not rendered");
 check(source.find("midi.addEvent(event.getMessage(),event.samplePosition-pos)")!=std::string::npos,"live MIDI chunk routing absent");
 check(source.find("Test VST3 instrument (C4)")!=std::string::npos,"instrument audition action missing");
 check(source.find("VST3 audio diagnostics")!=std::string::npos,"plugin audio diagnostics missing");
 const auto excludes=contents(root/"assets/V125_LEGACY_DUPLICATE_HIDES.txt");
 check(excludes.find("Browser-only exclusions")!=std::string::npos,"legacy duplicate policy undocumented");
 std::unordered_set<std::string> unique;
 for(size_t i=0,pos=0;i<excludes.size();i=pos){
  pos=excludes.find('\n',i);
  const auto line=excludes.substr(i,pos==std::string::npos?std::string::npos:pos-i);
  if(!line.empty()&&line[0]!='#'){
   check(unique.insert(line).second,"duplicate curation entry");
   check(fs::exists(root/"assets/samples"/line),"legacy compatibility WAV missing");
  }
  if(pos==std::string::npos)break;
  ++pos;
 }
 check(unique.size()==34,"expected curated legacy duplicate count");
 check(source.find("hiddenNearDuplicates.contains(f.getFileName())")!=std::string::npos,"browser curation not wired");
 std::cout<<"PASS: VST live-monitor integration and "<<unique.size()<<" curated legacy duplicate labels (Windows JUCE GUI not run)\n";
}
