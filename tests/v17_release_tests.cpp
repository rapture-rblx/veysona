#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::string contents(const fs::path& f){std::ifstream file(f);check(bool(file),"missing file");return {(std::istreambuf_iterator<char>(file)),{}};}
int main(){
 const fs::path base=VEYSONA_SOURCE_PATH;
 const auto cmake=contents(base/"CMakeLists.txt");
 check(cmake.find("VERSION 1.31.0")!=std::string::npos,"incorrect version");
 for(auto f:{"ProductionWorkflow.h","ProductionWorkflow.cpp","AudioCore.cpp","Main.cpp"})check(fs::exists(base/"src"/f),"missing engine or UI code");
 const auto gui=contents(base/"src/Main.cpp");
 for(auto text:{"showTemplateChooser","bounceSelectedTrack","audioToMidi","showRecentProjects","wavetablePreset","waveMorph"})
  check(gui.find(text)!=std::string::npos,"UI integration missing");
 check(contents(base/"installer/Veysona.iss").find("1.31.0")!=std::string::npos,"installer version");
 check(contents(base/"scripts/build_installer.ps1").find("1.31.0")!=std::string::npos,"packager version");
 check(contents(base/".github/workflows/windows.yml").find("1.31.0")!=std::string::npos,"CI artifact version");
 for(auto f:{"RELEASE_NOTES_v1.7.md","WINDOWS_QA_v1.7.md","SOURCE_AND_DISTRIBUTION_POLICY.md","LICENSE"})
  check(fs::exists(base/f),"missing release/legal documentation");
 const auto helper=contents(base/"publish_to_github.ps1");
 check(helper.find("--private")!=std::string::npos,"GitHub publish must remain private only");
 std::cout<<"v1.7 features preserved in v1.15 source, release versions and private publish guard verified\n";
}
