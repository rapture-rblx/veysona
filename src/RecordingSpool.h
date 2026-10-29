#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace veysona {
// Single-producer (audio thread), single-consumer (disk thread) stereo recorder.
// All buffers are allocated before capture. push() never blocks, allocates,
// acquires a mutex or touches the filesystem. Overflow is explicitly reported;
// no truncated take is ever presented as a successful recording.
class RecordingSpool {
public:
 explicit RecordingSpool(std::string rawPath, size_t ringCapacityFrames=1<<17);
 ~RecordingSpool();
 RecordingSpool(const RecordingSpool&)=delete;
 RecordingSpool& operator=(const RecordingSpool&)=delete;
 void start();
 bool push(const float* left,const float* right,size_t frames) noexcept;
 void finish(); // must run off the audio thread, after the final push
 bool failed() const noexcept {return failed_.load(std::memory_order_acquire);}
 size_t writtenFrames() const noexcept {return writtenFrames_.load(std::memory_order_acquire);}
 const std::string& path() const noexcept {return path_;}
 // Import is deliberately separate: a DAW clip currently uses RAM-backed audio.
 static std::vector<float> readInterleaved(const std::string& path,size_t frames);
private:
 void consume() noexcept;
 std::string path_;
 size_t capacity_;
 std::vector<float> ring_;
 std::ofstream stream_;
 std::thread worker_;
 alignas(64) std::atomic<size_t> read_{0};
 alignas(64) std::atomic<size_t> write_{0};
 std::atomic<size_t> writtenFrames_{0};
 std::atomic<bool> stopping_{false}, failed_{false}, started_{false};
};
}
