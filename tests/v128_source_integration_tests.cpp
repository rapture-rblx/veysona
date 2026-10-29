#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static std::string read(const fs::path& p){std::ifstream in(p);if(!in)throw std::runtime_error("Missing: "+p.string());return {std::istreambuf_iterator<char>(in),{}};}
static void contains(const std::string& s,const std::string& needle){if(s.find(needle)==std::string::npos)throw std::runtime_error("Missing GUI integration: "+needle);}
int main(){
 const fs::path root(VEYSONA_SOURCE_PATH);
 const auto app=read(root/"src/Main.cpp");
 const auto cmake=read(root/"CMakeLists.txt");
 contains(app,"showStartupSplash()");
 contains(app,"juce::Timer::callAfterDelay(1400");
 contains(app,"Veysona Studio");
 contains(app,"devices.createStateXml()");
 contains(app,"savedDeviceXml");
 contains(app,"busPeaks[b].store(busPeak");
 contains(app,"RETURN  ");
 contains(app,"const bool anySolo=std::any_of(session.tracks.begin()");
 contains(cmake,"VEYSONA_ENABLE_ASIO");
 contains(cmake,"JUCE_ASIO=1");
 contains(read(root/"src/AudioCore.h"),"constexpr size_t kMaxBuses=8;");
 contains(read(root/"src/AudioCore.cpp"),"int(send)==output");
 if(!fs::exists(root/"assets/branding/Veysona-Logo.png"))throw std::runtime_error("Logo missing");
 return 0;
}
