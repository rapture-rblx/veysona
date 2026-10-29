#include "RecordingSpool.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>
using namespace veysona;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
int main(){
 namespace fs=std::filesystem;
 const auto file=fs::temp_directory_path()/"veysona_v123_recording_spool.raw";
 std::vector<float> l(317),r(317);
 {
  RecordingSpool spool(file.string(),65536);
  spool.start();
  size_t produced=0;
  for(int block=0;block<160;++block){
   for(int i=0;i<317;++i){l[i]=float(std::sin((produced+i)*.021));r[i]=float(std::cos((produced+i)*.017));}
   CHECK(spool.push(l.data(),r.data(),l.size()));
   produced+=l.size();
  }
  spool.finish();
  CHECK(!spool.failed());CHECK(spool.writtenFrames()==produced);
  auto data=RecordingSpool::readInterleaved(file.string(),produced);
  CHECK(data.size()==produced*2);
  for(size_t i=0;i<produced;++i){CHECK(std::abs(data[2*i]-std::sin(i*.021))<1e-6);CHECK(std::abs(data[2*i+1]-std::cos(i*.017))<1e-6);}
 }
 // Longer than the old one-minute limit: audio is streamed in bounded
 // blocks while the capture duration is independent of the ring size.
 {
  constexpr size_t longFrames=48000*65;
  RecordingSpool spool(file.string(),1<<22);
  spool.start();
  std::vector<float> lblock(1024,.25f),rblock(1024,-.5f);
  for(size_t pos=0;pos<longFrames;pos+=1024){
   const auto n=std::min(size_t(1024),longFrames-pos);
   CHECK(spool.push(lblock.data(),rblock.data(),n));
  }
  spool.finish();CHECK(!spool.failed());CHECK(spool.writtenFrames()==longFrames);
  auto longData=RecordingSpool::readInterleaved(file.string(),longFrames);
  CHECK(longData.size()==2*longFrames);
  CHECK(longData.front()==.25f&&longData.back()==-.5f);
 }
 {
  RecordingSpool spool(file.string(),1024);
  spool.start();
  CHECK(!spool.push(l.data(),r.data(),1025));
  CHECK(spool.failed());
  spool.finish();
 }
 { // Explicit failure if an invalid source is provided.
  RecordingSpool spool(file.string(),1024);spool.start();
  CHECK(!spool.push(nullptr,nullptr,12));CHECK(spool.failed());spool.finish();
 }
 fs::remove(file);
 std::cout<<"PASS: real-time safe FIFO, stereo frames, 65-second spool, overflow and input errors\n";
}
