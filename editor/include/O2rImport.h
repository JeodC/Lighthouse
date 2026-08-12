#pragma once

#include <cstdint>
#include <libultraship/libultra/gbi.h>
#include <string>
#include <unordered_map>
#include <vector>

typedef struct bk_model_header_s BKModelBin;

namespace Lightbulb {
void RegisterBKFactories();
std::vector<std::string> ListO2rModelPaths(const std::string& dir = "model");
std::vector<std::string> ListO2rResourcePaths(const std::string& dir);
enum class MountResult { Failed, NeedsBase, Base, Romhack };
MountResult MountO2r(const std::string& path);
MountResult MountRomhackO2r(const std::string& path);
bool BaseO2rMounted();
BKModelBin* LoadO2rModel(const std::string& path);
void ResetModelCache();
void ResetSpriteCache();
void ResetAnimCache();
} // namespace Lightbulb

namespace Lightbulb {
struct BKLevel {
    uint16_t mapId;
    const char* name;
    uint16_t opaModel;
    uint16_t xluModel;
};
inline uint16_t BKLevelSetupAsset(const BKLevel& level) {
    return static_cast<uint16_t>(level.mapId + 0x71C);
}
extern const BKLevel kBKLevels[];
extern const int kBKLevelCount;
} // namespace Lightbulb

namespace Lightbulb {
struct SetupProp {
    uint16_t id = 0;
    uint8_t type = 0;
    int16_t pos[3] = { 0, 0, 0 };
    uint8_t yaw = 0;
    uint8_t roll = 0;
    uint8_t scale = 0;
    uint8_t flags = 0;
    uint8_t spritePhase = 0;
};
struct SetupNode {
    int16_t pos[3] = { 0, 0, 0 };
    uint16_t radius = 0;
    uint8_t category = 0;
    uint16_t id = 0;
    uint16_t yawRaw = 0;
    uint32_t scaleRaw = 0;
    uint8_t pathUid = 0;
    uint8_t pathNext = 0;
};
struct SetupCamera {
    int16_t index = 0;
    uint8_t type = 0;
    float pos[3] = { 0, 0, 0 };
    float pitchYawRoll[3] = { 0, 0, 0 };
};
struct SetupScene {
    std::string path;
    int cubeCount = 0;
    int32_t boundsMin[3] = { 0, 0, 0 };
    int32_t boundsMax[3] = { 0, 0, 0 };
    std::vector<SetupProp> props;
    std::vector<SetupNode> nodes;
    std::vector<SetupCamera> cameras;
    bool loaded = false;
};
bool LoadO2rSetup(const std::string& path, SetupScene& out);
} // namespace Lightbulb

namespace Lightbulb {
struct O2rSpriteChunk {
    const uint8_t* texels = nullptr;
    const uint8_t* tlut = nullptr;
    int width = 0, height = 0;
    int fmt = 0, siz = 0, tlutColors = 0;
    int posX = 0, posY = 0;
};
struct O2rSpriteFrame {
    std::vector<O2rSpriteChunk> chunks;
    int originX = 0, originY = 0;
    int frameW = 1, frameH = 1;
};
struct O2rSpriteTex {
    std::vector<O2rSpriteFrame> frames;
    float dispW = 0.0f, dispH = 0.0f;
    uint8_t animSpeed = 0, animType = 0;
    uint8_t animDir = 0, animFlip = 0;
    bool loaded = false;
};
bool LoadO2rSprite(uint32_t assetId, O2rSpriteTex& out);
bool LoadO2rSpriteByPath(const std::string& basePath, O2rSpriteTex& out);
std::vector<std::string> ListO2rSpritePaths();
struct SpriteFrame {
    int frame = 0;
    bool mirror = false;
};
SpriteFrame SpriteFrameAt(const O2rSpriteTex& sprite, double seconds, int phase);
} // namespace Lightbulb

namespace Lightbulb {
struct O2rAnimKey {
    int32_t time = 0;
    float val = 0.0f;
    uint8_t smooth = 0;
    uint8_t smoothNext = 0;
};
struct O2rAnimChannel {
    int32_t bone = 0;
    int32_t channel = 0;
    std::vector<O2rAnimKey> keys;
};
struct O2rAnim {
    int32_t startFrame = 0, endFrame = 0;
    std::vector<O2rAnimChannel> channels;
    int32_t maxBoneId = 0;
    bool loaded = false;
};
struct BonePose {
    float rot[3] = { 0, 0, 0 };
    float scale[3] = { 1, 1, 1 };
    float trans[3] = { 0, 0, 0 };
};
std::vector<std::string> ListO2rAnimPaths();

bool ModelHasAnimTable(uint32_t modelAsset);
bool ModelUsesAnim(uint32_t modelAsset, uint32_t animAsset);
float AnimDuration(uint32_t modelAsset, uint32_t animAsset);
bool LoadO2rAnim(const std::string& path, O2rAnim& out);
void SampleO2rAnim(const O2rAnim& anim, float progress, std::vector<BonePose>& bonesOut);
int BuildBoneMatrices(BKModelBin* model, const std::vector<BonePose>& bones, Mtx* out, int maxOut);
void TransformAnimVertices(BKModelBin* model, const Mtx* boneMtx, int boneCount);
} // namespace Lightbulb

namespace Lightbulb {
uint32_t ActorModelAsset(uint32_t actorId);
const char* ActorEnumName(uint32_t actorId);

void SetActorOverridesEnabled(bool on);
uint32_t ActorDisplayAsset(uint32_t actorId);
uint32_t ActorExtraModel(uint32_t actorId);
const char* EditorStandInModel(uint8_t category, uint32_t id);
bool EditorEntryPointId(uint32_t id);
bool ActorDrawTransform(uint32_t assetId, float& scale, float& yOff);
float ActorSpinRate(uint32_t actorId);
bool ActorPlacement(uint32_t actorId, float pos[3], float& yawDeg, float& scale);
} // namespace Lightbulb

namespace Lightbulb {
struct MapExtraModel {
    uint16_t modelId;
    float pos[3];
    float scale;
};
int MapExtraModels(uint16_t mapId, MapExtraModel out[3]);
struct SkyLayerInfo {
    uint32_t modelId;
    float scale;
    float rotSpeed;
};
int SkyLayersForMap(uint16_t mapId, SkyLayerInfo out[3]);
} // namespace Lightbulb
