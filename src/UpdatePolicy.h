#pragma once
#include <string_view>

namespace veysona::update {
struct Version { unsigned major=0, minor=0, patch=0; };
bool parseVersion(std::string_view text, Version& result) noexcept;
bool isNewer(std::string_view candidate, std::string_view installed) noexcept;
bool isHexDigest(std::string_view value, size_t length) noexcept;
bool isHttpsReleaseUrl(std::string_view value) noexcept;
// Automatic installation is disabled without a pinned, trusted publisher cert.
bool canInstallAutomatically(std::string_view publisherThumbprint) noexcept;
}
