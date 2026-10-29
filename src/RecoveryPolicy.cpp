#include "RecoveryPolicy.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace veysona::recovery {
int clampInterval(int minutes) { return std::clamp(minutes, kMinimumIntervalMinutes, kMaximumIntervalMinutes); }
bool shouldSave(std::int64_t nowMillis, std::int64_t lastMillis, int intervalMinutes,
                bool enabled, bool playing, bool recording) {
  if (!enabled || playing || recording || nowMillis < lastMillis) return false;
  return nowMillis - lastMillis >= std::int64_t(clampInterval(intervalMinutes)) * 60'000;
}
std::string safeName(std::string_view name) {
  std::string out;
  out.reserve(std::min(name.size(), std::size_t(48)));
  bool separator = false;
  for (unsigned char c : name) {
    const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (valid) { out.push_back(char(c)); separator = false; }
    else if (!separator && !out.empty()) { out.push_back('-'); separator = true; }
    if (out.size() == 48) break;
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "Untitled" : out;
}
std::string snapshotStem(std::string_view projectName, std::string_view identity) {
  // FNV-1a is deterministic across processes and does not expose absolute paths.
  std::uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : identity) { hash ^= c; hash *= 1099511628211ull; }
  std::ostringstream stream;
  stream << safeName(projectName) << "-" << std::hex << std::setw(16)
         << std::setfill('0') << hash;
  return stream.str();
}
std::string snapshotFilename(std::string_view projectName, std::string_view identity, int slot) {
  return snapshotStem(projectName, identity) + ".recovery." + std::to_string(std::clamp(slot, 0, kSlotCount - 1) + 1) + ".veys";
}
int nextSlot(int currentSlot) { return (currentSlot + 1 + kSlotCount) % kSlotCount; }
StartupAction chooseStartup(bool welcomeCompleted,int startupMode,bool lastProjectExists) {
  if (!welcomeCompleted || startupMode == 3) return StartupAction::Welcome;
  if (startupMode == 1) return StartupAction::Demo;
  if (startupMode == 2) return StartupAction::Blank;
  return lastProjectExists ? StartupAction::LastProject : StartupAction::Blank;
}
}
