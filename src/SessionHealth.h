#pragma once
#include "AudioCore.h"
#include <string>
#include <vector>

namespace veysona::health {
// Lightweight project preflight: no JUCE or filesystem access, suitable for a
// save/export warning dialog and deterministic CI regression tests.
enum class Severity { Information, Warning, Error };
struct Finding {
 Severity severity = Severity::Information;
 std::string code, location, message;
};
struct Report {
 std::vector<Finding> findings;
 int errors() const;
 int warnings() const;
 bool canExport() const { return errors() == 0; }
 std::string summary() const;
};
Report inspect(const Session& session);
}
