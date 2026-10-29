#ifdef NDEBUG
#undef NDEBUG // Assertions must be active in Release test executables.
#endif
#include "AudioCore.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cassert>
using namespace veysona;
namespace fs=std::filesystem;
static void verifyWav(const fs::path& path){
 auto clip=readWav(path.string());
 assert(clip.sampleRate==48000);
 assert(clip.stereo.size()>4800);
 assert(clip.stereo.size()%2==0);
 assert(!clip.waveformPeaks.empty());
 double energy=0;
 for(float s:clip.stereo){assert(std::isfinite(s));assert(std::abs(s)<=1.001f);energy+=std::abs(s);}
 assert(energy>3);
}
int main(){
 auto root=fs::path(VEYSONA_SOURCE_PATH);
 assert(fs::exists(root/"assets"/"branding"/"Veysona-Logo.png"));
 assert(fs::exists(root/"assets"/"branding"/"Veysona-Icon.png"));
 assert(fs::exists(root/"assets"/"branding"/"Veysona.ico"));
 assert(fs::exists(root/"installer"/"Veysona.iss"));
 int samples=0,instruments=0,midi=0;
 for(const auto& file:fs::directory_iterator(root/"assets"/"samples"))if(file.path().extension()==".wav"){verifyWav(file.path());++samples;}
 for(const auto& file:fs::directory_iterator(root/"assets"/"instruments"))if(file.path().extension()==".wav"){verifyWav(file.path());++instruments;}
 for(const auto& file:fs::directory_iterator(root/"assets"/"midi"))if(file.path().extension()==".mid"){
  std::ifstream f(file.path(),std::ios::binary);char bytes[4];f.read(bytes,4);
  assert(std::string(bytes,4)=="MThd");++midi;
 }
 assert(samples>=95 && instruments>=32 && midi>=10);
 assert(fs::exists(root/"assets"/"samples"/"Loop - UK Garage 132 BPM.wav"));
 assert(fs::exists(root/"assets"/"instruments"/"Nebula Pad C4.wav"));
 assert(fs::exists(root/"assets"/"midi"/"Veysona - Synth Hook.mid"));
 std::cout<<"Veysona branding + installer sources + "<<samples<<" samples + "
          <<instruments<<" sampled presets + "<<midi<<" MIDI files verified\n";
}
