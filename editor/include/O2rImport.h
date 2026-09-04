#pragma once

#include <cstdint>
#include <libultraship/libultra/gbi.h>
#include <map>
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
    int16_t cubeMin[3];
    int16_t cubeMax[3];
    float scale;
};
inline uint16_t BKLevelSetupAsset(const BKLevel& level) {
    return static_cast<uint16_t>(level.mapId + 0x71C);
}
extern const BKLevel kBKLevels[];
extern const int kBKLevelCount;
const BKLevel* FindBKLevel(uint16_t mapId);
const char* LevelEnumName(int level);
int VanillaMapLevel(int mapId);
// A warp arrives at whichever node carries the entry-point actor its exit id names.
uint32_t EntryActorForExit(uint32_t exitId);
bool ActorIsEntryPoint(uint32_t actorId);
} // namespace Lightbulb

namespace Lightbulb {
// The settings aGameConfig carries, in the order Torch keys them.
enum GameConfigKey {
    kNewGameMap = 0,
    // The map halves of the two warps below, cut from those same two bytes as a byte, not a word.
    kStartLevel1,
    kStartLevel2,
    kKnowAllMoves,
    kMumboCostTermite,
    kMumboCostCroc,
    kMumboCostWalrus,
    kMumboCostPumpkin,
    kMumboCostBee,
    kEggsMax,
    kRedFeathersMax,
    kGoldFeathersMax,
    kEggsCheatoMax,
    kRedFeathersCheatoMax,
    kGoldFeathersCheatoMax,
    kNotesMax,
    kJiggiesPerWorld,
    kHoneycombsPerWorld,
    kExtraHoneycombStart,
    kWarpExitBanjosHouse,
    kWarpEnterLair,
    kSpecialLevel,
    kHideJiggiesLevel,
    kHideCollectiblesLevel,
    kGameConfigKeyCount
};
// The halfword a hack's instruction patch leaves in the config. Lighthouse reads the key's
// presence, not this value, and keeps it only so a written config carries what a hack would.
constexpr int kKnowAllMovesOn = 0x0F98;

// A romhack's settings, as Torch writes them to assets/aGameConfig. Per-map tables hold only
// the rows the hack changed; the game's own tables stand for the rest.
struct GameConfig {
    std::string romName;
    int constants[kGameConfigKeyCount] = {};
    // Which constants the blob actually carried. The game's own value stands for the rest, so a
    // writer emits only these - and for kKnowAllMoves the flag is the setting.
    bool constantSet[kGameConfigKeyCount] = {};
    int noteDoors[12] = {};
    struct JiggyPuzzle {
        int cost = 0, size = 0, flag = 0;
    };
    JiggyPuzzle puzzles[11];
    std::string levelNames[13];
    struct ReturnToLair {
        int map = -1, exit = -1;
    };
    ReturnToLair returnToLair[11];
    struct MusicRow {
        int track1 = 0, track2 = 0;
    };
    struct SkyRow {
        int models[3] = { 0, 0, 0 };
        float scales[3] = { 1, 1, 1 };
        float rotations[3] = { 0, 0, 0 };
    };
    struct SceneDef {
        int opa = 0, xlu = 0;
        int cubeMin[3] = { 0, 0, 0 };
        int cubeMax[3] = { 0, 0, 0 };
        float scale = 1.0f;
    };
    std::map<int, int> sceneRemap; // map -> level
    std::map<int, MusicRow> music; // by map
    std::map<int, SkyRow> skybox;  // by map
    std::map<int, SceneDef> sceneDefs;
    std::map<int, int> warpDests; // warp index -> map << 8 | entry
    int customCodeKind = 0;
    uint32_t customCodeRamBase = 0;
    std::string customCodeSha1;
    std::string romSha1;
    bool fromArchive = false;
};
GameConfig VanillaGameConfig();
// Vanilla, then whatever the mounted archives' aGameConfig lays over it. False when there is none.
bool LoadO2rGameConfig(GameConfig& out);
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
    // A scripted path waypoint in the node slot: the fields above are junk, the leg below is the record.
    // Split per Struct_glspline_t1 in spline_pathfollow.c.
    uint8_t script = 0;
    float legFraction = 0.0f; // 0..1 along the path; the leg fires once the actor passes it
    uint8_t legApply = 0;     // bit0 pause, bit1 speed, bit2 animation
    uint16_t legSpeed = 0;    // quarter units
    uint16_t legPause = 0;    // quarter units, or an alternate value when legPauseIsAlt
    uint8_t legPauseIsAlt = 0;
    uint16_t legAnim = 0;         // 10-bit animation table index
    uint16_t legAnimDuration = 0; // quarter units
    uint8_t legAnimMode = 0;      // 2 once, 3 once reversed, 4 loop, 5 loop reversed, 6 hold
    uint8_t legHeadingMode = 0;   // 1 face path; 2..7 pick yaw/pitch below, 7 = both
    uint16_t legYaw = 0;          // degrees
    uint16_t legPitch = 0;        // degrees
    uint16_t legLinkUid = 0;      // another waypoint to blend toward, 0 = none
    uint8_t legBlend = 0;         // bit0 blend heading toward link, bit1 blend speed
    uint8_t legModeBits = 0;
    uint8_t legSmoothTurn = 0;
    uint8_t legNoHeadingLookup = 0;
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
    // Pick box around the origin that covers every frame, at scale 1.
    float pickHalfWidth = 0.0f, pickLowY = 0.0f, pickHighY = 0.0f;
    bool loaded = false;
};
// Where a chunk sits around the frame origin, in display units; pixelAspect keeps texel units.
inline void SpriteChunkRect(const O2rSpriteTex& sprite, const O2rSpriteFrame& frame, const O2rSpriteChunk& chunk,
                            bool pixelAspect, float& x0, float& x1, float& y0, float& y1) {
    const float scaleX = pixelAspect ? 1.0f : sprite.dispW / (float)frame.frameW;
    const float scaleY = pixelAspect ? 1.0f : sprite.dispH / (float)frame.frameH;
    x0 = (float)(chunk.posX - frame.originX) * scaleX;
    x1 = (float)(chunk.posX - frame.originX + chunk.width - 1) * scaleX;
    y1 = (float)(frame.originY - chunk.posY) * scaleY;
    y0 = (float)(frame.originY - chunk.posY - (chunk.height - 1)) * scaleY;
}
// Both point into a cache that lives until the archive changes.
const O2rSpriteTex* LoadO2rSprite(uint32_t assetId);
const O2rSpriteTex* LoadO2rSpriteByPath(const std::string& basePath);
std::vector<std::string> ListO2rSpritePaths();
struct SpriteFrame {
    int frame = 0;
    bool mirror = false;
};
SpriteFrame SpriteFrameAt(const O2rSpriteTex& sprite, double seconds, int phase);
int SpriteRestFrame(const O2rSpriteTex& sprite);
void LoadO2rGuiTexture(const char* name, const char* texturePath, const char* palettePath);
void* O2rGuiTexture(const char* name, float& outWidth, float& outHeight);
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
const O2rAnim* LoadO2rAnim(const std::string& path);
void SampleO2rAnim(const O2rAnim& anim, float progress, std::vector<BonePose>& bonesOut);
int BuildBoneMatrices(BKModelBin* model, const std::vector<BonePose>& bones, Mtx* out, int maxOut);
void TransformAnimVertices(BKModelBin* model, const Mtx* boneMtx, int boneCount);
} // namespace Lightbulb

namespace Lightbulb {
uint32_t ActorModelAsset(uint32_t actorId);
bool ActorHasModelInfo(uint32_t actorId);
const char* ActorEnumName(uint32_t actorId);

void SetActorOverridesEnabled(bool on);

bool StartAudioEngine();
void PumpAudioEngine();
bool AudioEngineRunning();
void PlayMusicTrack(int trackId);
void StopMusic();
void ReleaseMusicTracks();
int LevelMusicTrack(uint16_t mapId);
int LevelMusicTrack2(uint16_t mapId);
void StartLevelMusic(uint16_t mapId);
void StopLevelMusic();
void SetAudioListener(const float pos[3], uint16_t mapId, BKModelBin* opaque, BKModelBin* translucent);
int MusicChannelMask();
int MusicListenerMap();
bool MusicSlotIdle();
uint32_t ActorDisplayAsset(uint32_t actorId);
uint32_t ActorExtraModel(uint32_t actorId);
const char* EditorStandInModel(uint8_t category, uint32_t id);
bool EditorStandInInsidePole(uint8_t category, uint32_t id);
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
bool ActorIsSpawnable(uint32_t actorId);
bool ActorRegisteredForMap(uint16_t mapId, uint32_t actorId);
struct SkyLayerInfo {
    uint32_t modelId;
    float scale;
    float rotSpeed;
};
int SkyLayersForMap(uint16_t mapId, SkyLayerInfo out[3]);

// Where a warp sends Banjo with no override, or -1 when its handler works the destination out.
int VanillaWarpDest(int warp);

// Every warp the game names, in slot order. The name carries the world it belongs to.
int WarpCount();
int WarpIdAt(int index);
const char* WarpName(int index);

// The skybox and cloud models the game ships, for picking one by name.
int SkyModelCount();
uint32_t SkyModelId(int index);
const char* SkyModelName(uint32_t id);
} // namespace Lightbulb

namespace Lightbulb {
struct O2rSound {
    std::string path;
    uint8_t samplePan = 0;
    uint8_t sampleVolume = 0;
    uint8_t flags = 0;
    bool hasEnvelope = false;
    int32_t attackTime = 0;
    int32_t decayTime = 0;
    int32_t releaseTime = 0;
    uint8_t attackVolume = 0;
    uint8_t decayVolume = 0;
    bool hasKeyMap = false;
    uint8_t velocityMin = 0;
    uint8_t velocityMax = 0;
    uint8_t keyMin = 0;
    uint8_t keyMax = 0;
    uint8_t keyBase = 60;
    int8_t detune = 0;
    bool hasWave = false;
    uint8_t waveType = 0;
    uint8_t waveFlags = 0;
    bool hasLoop = false;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    uint32_t loopCount = 0;
    bool hasBook = false;
    int32_t bookOrder = 0;
    int32_t bookNpredictors = 0;
    uint32_t encodedBytes = 0;
    std::vector<int16_t> pcm;
    uint32_t userCount = 0;
};

constexpr int kSoundBankRate = 22050;

std::vector<std::string> ListO2rSoundPaths(const std::string& dir);
bool LoadO2rSound(const std::string& path, O2rSound& out);
void ResetSoundCache();

inline int SoundChainTarget(const O2rSound& s) {
    const int next = s.velocityMin + ((s.keyMin & 0xC0) * 4);
    return next ? next - 1 : -1;
}
inline int SoundChainDelayFrames(const O2rSound& s) {
    return s.velocityMax;
}
inline int SoundVolumeGroup(const O2rSound& s) {
    return s.keyMin & 0x3F;
}
inline int SoundReverbSend(const O2rSound& s) {
    return s.keyMax & 0x0F;
}
inline bool SoundIsPositional(const O2rSound& s) {
    return s.hasEnvelope && s.decayTime == -1;
}
float SoundPitchRatio(const O2rSound& s);
} // namespace Lightbulb
