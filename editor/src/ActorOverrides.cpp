#include "O2rImport.h"

extern "C" {
#include "enums.h"
}

namespace Lightbulb {
namespace {
struct PlacementRow {
    uint32_t actor;
    uint8_t posMask;
    float pos[3];
    uint8_t hasYaw;
    float yaw;
    uint8_t hasScale;
    float scale;
};

const PlacementRow kPlacements[] = {
#include "importer/ActorPlacement.inc"
};

bool sEnabled = true;

} // namespace

void SetActorOverridesEnabled(bool on) {
    sEnabled = on;
}

bool EditorEntryPointId(uint32_t id) {
    return ActorIsEntryPoint(id);
}

// The game draws nothing for these, so the editor substitutes marker models.
// Categories match the node word's bit6 field; see NodeProp in prop.h.
const char* EditorStandInModel(uint8_t category, uint32_t id) {
    switch (category) {
        case 3:
            return "editor/warp";
        case 4:
            // A contact trigger's id picks its handler, so the marker follows the id
            // rather than the category. 0x16-0x29 start the area camera for area
            // (id - 0x16), which is defined by the 0xCC+area node below.
            if (id >= 0x16 && id <= 0x29) {
                return "editor/cam_start";
            }
            if (id == 0x2A) {
                return "editor/cam_end";
            }
            if (id == 0x4C || id == 0x4D) {
                return "editor/magic_marker";
            }
            return nullptr;
        case 7:
            return "editor/enemy_marker";
        case 8:
            return "editor/path_marker";
        default:
            break;
    }
    if (category != 6) {
        return nullptr;
    }
    if (EditorEntryPointId(id)) {
        return "editor/entry";
    }
    switch (id) {
        // 0x26 is the base of a climb; 0x27 and 0x28 are both tops, and the base
        // takes whichever is nearest. Reaching a 0x28 top lets Banjo climb off.
        case 0x26:
            return "editor/climb_start";
        case 0x27:
        case 0x28:
            return "editor/climb_end";
        case 0x66:
            return "editor/cam_controller";
        case 0x184:
        case 0x185:
        case 0x186:
            return "editor/walkin";
        case 0x192:
            return "editor/reverb";
        default:
            break;
    }
    if (id >= 0xCC && id <= 0xDF) {
        return "editor/cam_controller";
    }
    return nullptr;
}

bool EditorStandInInsidePole(uint8_t category, uint32_t id) {
    return category == 6 && (id == 0x26 || id == 0x27 || id == 0x28);
}

uint32_t ActorDisplayAsset(uint32_t actorId) {
    if (sEnabled) {
        switch (actorId) {
            case 0x37A:
            case 0x12B:
                return 0x388;
            case 0x11:
                return 0x2E6;
            case 0x2E4:
                return 0x55A;
            default:
                break;
        }
    }
    return ActorModelAsset(actorId);
}

uint32_t ActorExtraModel(uint32_t actorId) {
    if (!sEnabled) {
        return 0;
    }
    switch (actorId) {
        case 0x2E2:
            return 0x3BE;
        case 0x15F:
            return 0x426;
        default:
            return 0;
    }
}

bool ActorDrawTransform(uint32_t assetId, float& scale, float& yOff) {
    if (!sEnabled) {
        return false;
    }
    switch (assetId) {
        case 0x3EA:
            scale = 0.4f;
            yOff = 4.0f;
            return true;
        case 0x3E9:
            yOff = 8.0f;
            return true;
        case 0x3EB:
            yOff = 8.0f;
            return true;
        default:
            return false;
    }
}

float ActorSpinRate(uint32_t actorId) {
    if (!sEnabled) {
        return 0.0f;
    }
    switch (actorId) {
        case 0x46:
            return 230.0f;
        case 0x47:
            return 200.0f;
        case 0x50:
            return 200.0f;
        default:
            return 0.0f;
    }
}

bool ActorPlacement(uint32_t actorId, float pos[3], float& yawDeg, float& scale) {
    if (!sEnabled) {
        return false;
    }

    if (actorId == 0x25C) {
        pos[0] += 0.412f * (8831.0f - pos[0]);
        pos[2] += 0.412f * (13535.0f - pos[2]);
        pos[1] = 700.0f;
        yawDeg = 199.0f;
        return true;
    }

    for (const PlacementRow& r : kPlacements) {
        if (r.actor != actorId) {
            continue;
        }
        if (r.posMask & 1) {
            pos[0] = r.pos[0];
        }
        if (r.posMask & 2) {
            pos[1] = r.pos[1];
        }
        if (r.posMask & 4) {
            pos[2] = r.pos[2];
        }
        if (r.hasYaw) {
            yawDeg = r.yaw;
        }
        if (r.hasScale) {
            scale = r.scale;
        }
        return true;
    }
    return false;
}

} // namespace Lightbulb
