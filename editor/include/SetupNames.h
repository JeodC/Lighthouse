#pragma once

#include "O2rImport.h"

#include <cstdint>

namespace Lightbulb {
inline const char* CameraTypeName(uint8_t type) {
    static const char* kNames[] = { "?", "Pivot", "Static", "Zoom", "Random" };
    return type < 5 ? kNames[type] : "?";
}

inline const char* NodeCategoryName(uint8_t category) {
    switch (category) {
        case 3:
            return "Warp";
        case 4:
            return "Contact trigger";
        case 6:
            return "Actor spawn";
        case 7:
            return "Enemy boundary";
        case 8:
            return "Path node";
        case 9:
            return "Camera trigger";
        case 10:
            return "Flag";
        default:
            return "Node";
    }
}

inline const char* PropKindName(uint8_t type) {
    return type == 2 ? "Model" : type == 0 ? "Sprite" : "Actor";
}

inline const char* NodeKindName(const SetupNode& node) {
    return node.script ? "Script waypoint" : NodeCategoryName(node.category);
}
} // namespace Lightbulb
