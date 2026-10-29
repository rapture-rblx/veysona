#pragma once
#include "AudioCore.h"
#include <cstdint>
#include <vector>

namespace veysona::groove {
enum class Style { House, Garage, Breakbeat, Trap };
struct Settings {
 Style style=Style::House;
 int bars=4;
 int stepsPerBar=16;
 double swing=0.12; // shifts odd 16th notes within the step; 0..0.45
 double density=.65; // controls decorative hats and ghost notes
 std::uint32_t seed=12345;
};
// Deterministic, editable MIDI output using GM drum notes 36/38/39/42/46.
// No external sample library or dynamic runtime/plugin dependency.
std::vector<Note> generate(const Settings& settings);
}
