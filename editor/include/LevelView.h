#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Shared between the level panel and the scene it builds.
namespace Lightbulb {
constexpr float kDeg = 3.14159265f / 180.0f;

// Shared by the level projection and the click ray; they must not drift apart.
constexpr float kLevelFovYDeg = 55.0f;

// Asset paths end in ASSET_<hex>_<name>; key them by that id.
inline std::map<uint32_t, std::string> indexById(const std::vector<std::string>& paths) {
    std::map<uint32_t, std::string> idx;
    for (const std::string& path : paths) {
        const size_t slash = path.find_last_of('/');
        const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
        if (std::strncmp(name, "ASSET_", 6) == 0) {
            idx.emplace(static_cast<uint32_t>(std::strtoul(name + 6, nullptr, 16)), path);
        }
    }
    return idx;
}
} // namespace Lightbulb
