#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <stdexcept>
namespace fs=std::filesystem;
static std::string read(const fs::path& f){std::ifstream in(f);if(!in)throw std::runtime_error("Missing file "+f.string());return {std::istreambuf_iterator<char>{in},{}};}
static void require(bool test,const char* desc){if(!test)throw std::runtime_error(desc);}
int main(){
 const fs::path root=VEYSONA_SOURCE_PATH;
 const auto gui=read(root/"src/Main.cpp");
 require(gui.find("restoreSavedPlugins()")!=std::string::npos,"missing plugin restore workflow");
 require(gui.find("assetsDirectory")!=std::string::npos,"missing generation-based project media storage");
 require(gui.find("TemporaryFile tempOutput(output)")!=std::string::npos,"missing transactional WAV output");
 require(gui.find("WavStreamWriter writer")!=std::string::npos,"missing streaming export integration");
 require(gui.find("std::min(300.")==std::string::npos,"legacy five-minute render cap remains");
 require(gui.find("if(!renderExport(f))")!=std::string::npos,"stem export incorrectly counts failed files");
 const auto installer=read(root/"installer/Veysona.iss");
 require(installer.find("1.31.0")!=std::string::npos,"installer version mismatch");
 require(installer.find("Veysona.ico")!=std::string::npos,"installer branding absent");
 const auto cmake=read(root/"CMakeLists.txt");
 require(cmake.find("VERSION 1.31.0")!=std::string::npos,"build version mismatch");
 std::cout<<"PASS: project save/export and plugin restore integration source contracts (GUI runtime unverified)\n";
}
