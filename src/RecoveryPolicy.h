#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace veysona::recovery {
// No UI or filesystem dependencies; used by the Windows app and regression tests.
constexpr int kMinimumIntervalMinutes = 1;
constexpr int kMaximumIntervalMinutes = 30;
constexpr int kSlotCount = 3;

int clampInterval(int minutes);
bool shouldSave(std::int64_t nowMillis, std::int64_t lastMillis, int intervalMinutes,
                bool enabled, bool playing, bool recording);
std::string safeName(std::string_view name);
std::string snapshotStem(std::string_view projectName, std::string_view identity);
std::string snapshotFilename(std::string_view projectName, std::string_view identity, int slot);
int nextSlot(int currentSlot);
enum class StartupAction { Welcome, LastProject, Demo, Blank };
StartupAction chooseStartup(bool welcomeCompleted,int startupMode,bool lastProjectExists);
}
