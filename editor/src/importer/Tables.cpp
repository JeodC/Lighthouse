#include "O2rImport.h"

#include <algorithm>
#include <cstdint>

namespace Lightbulb {
namespace {
struct ActorName {
    uint32_t actor;
    const char* name;
};
const ActorName kActorNames[] = {
#include "ActorNames.inc"
};

struct ActorModel {
    uint32_t actor;
    uint32_t asset;
};
const ActorModel kActorModels[] = {
#include "ActorModels.inc"
};

struct MapExtras {
    uint16_t map;
    MapExtraModel models[3];
};
const MapExtras kMapExtras[] = {
    { 0x0B, { { 0x88E, { 5500.0f, 0.0f, 0.0f }, 1.0f } } },
};

struct SpawnSetRow {
    uint16_t overlay;
    uint16_t actor;
};
const SpawnSetRow kSpawnSets[] = {
#include "SpawnSets.inc"
};

struct MapOverlayRow {
    uint16_t map;
    uint16_t overlay;
};
const MapOverlayRow kMapOverlays[] = {
#include "MapOverlays.inc"
};

bool spawnSetHas(uint16_t overlay, uint32_t actorId) {
    const SpawnSetRow* end = kSpawnSets + sizeof(kSpawnSets) / sizeof(kSpawnSets[0]);
    const SpawnSetRow key = { overlay, (uint16_t)actorId };
    const SpawnSetRow* found =
        std::lower_bound(kSpawnSets, end, key, [](const SpawnSetRow& row, const SpawnSetRow& want) {
            return row.overlay != want.overlay ? row.overlay < want.overlay : row.actor < want.actor;
        });
    return found != end && found->overlay == overlay && found->actor == actorId;
}
} // namespace
} // namespace Lightbulb

namespace Lightbulb {
static uint32_t lookup(const ActorModel* table, size_t count, uint32_t actorId) {
    const ActorModel* end = table + count;
    const ActorModel* found =
        std::lower_bound(table, end, actorId, [](const ActorModel& row, uint32_t id) { return row.actor < id; });
    return (found != end && found->actor == actorId) ? found->asset : 0;
}

uint32_t ActorModelAsset(uint32_t actorId) {
    return lookup(kActorModels, sizeof(kActorModels) / sizeof(kActorModels[0]), actorId);
}

const char* ActorEnumName(uint32_t actorId) {
    const ActorName* end = kActorNames + sizeof(kActorNames) / sizeof(kActorNames[0]);
    const ActorName* found =
        std::lower_bound(kActorNames, end, actorId, [](const ActorName& row, uint32_t id) { return row.actor < id; });
    return (found != end && found->actor == actorId) ? found->name : nullptr;
}

bool ActorIsSpawnable(uint32_t actorId) {
    for (const SpawnSetRow& row : kSpawnSets) {
        if (row.actor == actorId) {
            return true;
        }
    }
    return false;
}

bool ActorRegisteredForMap(uint16_t mapId, uint32_t actorId) {
    uint16_t overlay = 0;
    for (const MapOverlayRow& row : kMapOverlays) {
        if (row.map == mapId) {
            overlay = row.overlay;
            break;
        }
    }
    return spawnSetHas(0, actorId) || (overlay != 0 && spawnSetHas(overlay, actorId));
}

int MapExtraModels(uint16_t mapId, MapExtraModel out[3]) {
    for (const MapExtras& extras : kMapExtras) {
        if (extras.map == mapId) {
            int written = 0;
            for (int slot = 0; slot < 3; ++slot) {
                if (extras.models[slot].modelId != 0) {
                    out[written++] = extras.models[slot];
                }
            }
            return written;
        }
    }
    return 0;
}

extern "C" int lb_skyLayers(int mapId, short outModel[3], float outScale[3], float outRotSpeed[3]);

int SkyLayersForMap(uint16_t mapId, SkyLayerInfo out[3]) {
    short models[3];
    float scales[3], rots[3];
    const int count = lb_skyLayers(mapId, models, scales, rots);
    for (int layer = 0; layer < count; layer++) {
        out[layer].modelId = static_cast<uint32_t>(models[layer]);
        out[layer].scale = scales[layer];
        out[layer].rotSpeed = rots[layer];
    }
    return count;
}

} // namespace Lightbulb
