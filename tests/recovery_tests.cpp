#include "RecoveryPolicy.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool ok, const char* message) {
 if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
 using namespace veysona::recovery;
 require(clampInterval(0)==1 && clampInterval(300)==30,"clamp interval");
 require(!shouldSave(120000,0,2,false,false,false),"disabled");
 require(!shouldSave(120000,0,2,true,true,false),"playing");
 require(!shouldSave(120000,0,2,true,false,true),"recording");
 require(!shouldSave(119999,0,2,true,false,false),"too early");
 require(shouldSave(120000,0,2,true,false,false),"exact due");
 require(!shouldSave(1000,2000,2,true,false,false),"clock rollback");
 require(safeName("my/invalid:<file>?name") == "my-invalid-file-name", "safe filenames");
 require(safeName("***")=="Untitled", "empty fallback");
 const auto a=snapshotFilename("Demo Song", "C:/Music/Demo Song.veys",0);
 require(a==snapshotFilename("Demo Song", "C:/Music/Demo Song.veys",0),"stable hash");
 require(a!=snapshotFilename("Demo Song", "C:/Elsewhere/Demo Song.veys",0),"separate projects");
 require(a!=snapshotFilename("Demo Song", "C:/Music/Demo Song.veys",1),"rotating slots");
 require(nextSlot(-1)==0 && nextSlot(0)==1 && nextSlot(1)==2 && nextSlot(2)==0,"slot rotation");
 require(chooseStartup(false,0,true)==StartupAction::Welcome,"first run welcome");
 require(chooseStartup(true,3,true)==StartupAction::Welcome,"always ask startup");
 require(chooseStartup(true,1,true)==StartupAction::Demo,"load demo preference");
 require(chooseStartup(true,2,true)==StartupAction::Blank,"blank preference");
 require(chooseStartup(true,0,true)==StartupAction::LastProject,"restore last project");
 require(chooseStartup(true,0,false)==StartupAction::Blank,"missing last project fallback");
 std::cout << "recovery/startup policy OK\n";
}
