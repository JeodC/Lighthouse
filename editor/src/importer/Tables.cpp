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

struct MapLevelRow {
    uint16_t map;
    uint16_t level;
};
const MapLevelRow kMapLevels[] = {
#include "MapLevels.inc"
};

// A warp's exit id and the entry-point actor the destination map must place for it,
// straight from func_803084F0.
struct ExitActor {
    uint16_t exit;
    uint16_t actor;
};
const ExitActor kExitActors[] = {
#include "ExitActors.inc"
};

struct SkyModel {
    uint32_t id;
    const char* name;
};
const SkyModel kSkyModels[] = {
#include "SkyModels.inc"
};

struct WarpName {
    uint16_t warp;
    const char* name;
};
const WarpName kWarpNames[] = {
#include "WarpNames.inc"
};

struct WarpDest {
    uint16_t warp;
    uint16_t dest;
};
const WarpDest kWarpDests[] = {
#include "WarpDests.inc"
};

struct LevelName {
    uint32_t level;
    const char* name;
};
const LevelName kLevelNames[] = {
#include "LevelEnum.inc"
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

const ActorModel* findActorModel(uint32_t actorId) {
    const ActorModel* end = kActorModels + sizeof(kActorModels) / sizeof(kActorModels[0]);
    const ActorModel* found =
        std::lower_bound(kActorModels, end, actorId, [](const ActorModel& row, uint32_t id) { return row.actor < id; });
    return (found != end && found->actor == actorId) ? found : nullptr;
}
} // namespace
} // namespace Lightbulb

namespace Lightbulb {
uint32_t ActorModelAsset(uint32_t actorId) {
    const ActorModel* found = findActorModel(actorId);
    return found ? found->asset : 0;
}

bool ActorHasModelInfo(uint32_t actorId) {
    return findActorModel(actorId) != nullptr;
}

const char* ActorEnumName(uint32_t actorId) {
    const ActorName* end = kActorNames + sizeof(kActorNames) / sizeof(kActorNames[0]);
    const ActorName* found =
        std::lower_bound(kActorNames, end, actorId, [](const ActorName& row, uint32_t id) { return row.actor < id; });
    return (found != end && found->actor == actorId) ? found->name : nullptr;
}

// Asked per node per frame, so the spawn sets are flattened to one sorted list once.
bool ActorIsSpawnable(uint32_t actorId) {
    static const std::vector<uint16_t> actors = [] {
        std::vector<uint16_t> ids;
        for (const SpawnSetRow& row : kSpawnSets) {
            ids.push_back(row.actor);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }();
    return actorId <= 0xFFFF && std::binary_search(actors.begin(), actors.end(), (uint16_t)actorId);
}

uint32_t EntryActorForExit(uint32_t exitId) {
    for (const ExitActor& row : kExitActors) {
        if (row.exit == exitId) {
            return row.actor;
        }
    }
    return 0;
}

bool ActorIsEntryPoint(uint32_t actorId) {
    for (const ExitActor& row : kExitActors) {
        if (row.actor == actorId) {
            return true;
        }
    }
    return false;
}

const char* LevelEnumName(int level) {
    for (const LevelName& row : kLevelNames) {
        if ((int)row.level == level) {
            return row.name;
        }
    }
    return nullptr;
}

int VanillaMapLevel(int mapId) {
    for (const MapLevelRow& row : kMapLevels) {
        if ((int)row.map == mapId) {
            return row.level;
        }
    }
    return -1;
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

int SkyModelCount() {
    return (int)(sizeof(kSkyModels) / sizeof(kSkyModels[0]));
}

uint32_t SkyModelId(int index) {
    return kSkyModels[index].id;
}

const char* SkyModelName(uint32_t id) {
    for (const SkyModel& row : kSkyModels) {
        if (row.id == id) {
            return row.name;
        }
    }
    return nullptr;
}

int WarpCount() {
    return (int)(sizeof(kWarpNames) / sizeof(kWarpNames[0]));
}

int WarpIdAt(int index) {
    return kWarpNames[index].warp;
}

const char* WarpName(int index) {
    return kWarpNames[index].name;
}

int VanillaWarpDest(int warp) {
    for (const WarpDest& row : kWarpDests) {
        if (row.warp == warp) {
            return row.dest;
        }
    }
    return -1;
}

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
