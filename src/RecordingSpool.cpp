#include "RecordingSpool.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace veysona {
RecordingSpool::RecordingSpool(std::string rawPath,size_t ringCapacityFrames)
 :path_(std::move(rawPath)),capacity_(ringCapacityFrames),ring_(ringCapacityFrames*2){
 if(capacity_<1024 || capacity_>size_t(1<<22))throw std::invalid_argument("Invalid recording ring capacity");
}
RecordingSpool::~RecordingSpool(){finish();}
void RecordingSpool::start(){
 if(started_.exchange(true))throw std::logic_error("Recording already started");
 stream_.open(path_,std::ios::binary|std::ios::trunc);
 if(!stream_){started_=false;throw std::runtime_error("Unable to create recording file");}
 try{worker_=std::thread(&RecordingSpool::consume,this);}
 catch(...){stream_.close();started_=false;throw;}
}
bool RecordingSpool::push(const float* left,const float* right,size_t frames) noexcept {
 if(!started_.load(std::memory_order_acquire)||stopping_.load(std::memory_order_relaxed)||failed_.load(std::memory_order_relaxed))return false;
 if(frames && !left){failed_.store(true,std::memory_order_release);return false;}
 const auto w=write_.load(std::memory_order_relaxed);
 const auto r=read_.load(std::memory_order_acquire);
 if(frames>capacity_-(w-r)){
  failed_.store(true,std::memory_order_release);return false;
 }
 for(size_t i=0;i<frames;++i){
  const size_t dst=((w+i)%capacity_)*2;
  const float l=left[i],rSample=right?right[i]:l;
  ring_[dst]=std::isfinite(l)?l:0.f;
  ring_[dst+1]=std::isfinite(rSample)?rSample:0.f;
 }
 write_.store(w+frames,std::memory_order_release);
 return true;
}
void RecordingSpool::consume() noexcept {
 while(true){
  const auto r=read_.load(std::memory_order_relaxed);
  const auto w=write_.load(std::memory_order_acquire);
  if(r==w){
   if(stopping_.load(std::memory_order_acquire))break;
   std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;
  }
  size_t n=std::min({w-r,capacity_-(r%capacity_),size_t(8192)});
  stream_.write(reinterpret_cast<const char*>(ring_.data()+2*(r%capacity_)),std::streamsize(n*2*sizeof(float)));
  if(!stream_){failed_.store(true,std::memory_order_release);break;}
  writtenFrames_.fetch_add(n,std::memory_order_release);
  read_.store(r+n,std::memory_order_release);
 }
 stream_.flush();if(!stream_)failed_.store(true,std::memory_order_release);
}
void RecordingSpool::finish(){
 stopping_.store(true,std::memory_order_release);
 if(worker_.joinable())worker_.join();
 if(stream_.is_open()){stream_.close();if(!stream_)failed_.store(true,std::memory_order_release);}
}
std::vector<float> RecordingSpool::readInterleaved(const std::string& path,size_t frames){
 if(frames>std::vector<float>().max_size()/2)throw std::length_error("Recording is too large to import");
 std::ifstream file(path,std::ios::binary|std::ios::ate);
 if(!file)throw std::runtime_error("Recording file missing");
 if(uint64_t(file.tellg())!=uint64_t(frames)*2*sizeof(float))throw std::runtime_error("Recording file is incomplete");
 file.seekg(0);
 std::vector<float> audio(frames*2);
 if(!file.read(reinterpret_cast<char*>(audio.data()),std::streamsize(audio.size()*sizeof(float))))
  throw std::runtime_error("Unable to read recording file");
 return audio;
}
}
