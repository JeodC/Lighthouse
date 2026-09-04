#include "O2rImport.h"

#include <cstdio>
#include <cstring>
#include <ship/Context.h>
#include <ship/resource/File.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <string>

namespace Lightbulb {
namespace {
constexpr uint32_t kMagic = 0x46434B42; // "BKCF"

// Little-endian, as Torch writes it. Running off the end pins the cursor at the end.
struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool has(size_t bytes) const {
        return pos + bytes <= size;
    }
    void skip(size_t bytes) {
        pos = has(bytes) ? pos + bytes : size;
    }
    uint8_t readU8() {
        return has(1) ? data[pos++] : 0;
    }
    uint16_t readU16() {
        if (!has(2)) {
            pos = size;
            return 0;
        }
        const uint16_t value = (uint16_t)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        return value;
    }
    uint32_t readU32() {
        if (!has(4)) {
            pos = size;
            return 0;
        }
        const uint32_t value = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
        pos += 4;
        return value;
    }
    float readF32() {
        const uint32_t bits = readU32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    std::string str(size_t length) {
        if (!has(length)) {
            pos = size;
            return std::string();
        }
        std::string value((const char*)data + pos, length);
        pos += length;
        return value;
    }
    std::string hex(size_t length) {
        std::string out;
        for (size_t i = 0; i < length; ++i) {
            char byte[3];
            std::snprintf(byte, sizeof(byte), "%02x", readU8());
            out += byte;
        }
        return out;
    }
};
} // namespace

GameConfig VanillaGameConfig() {
    GameConfig cfg;
    // Know-all-moves is the untouched instruction halfword; it counts as off by being unset.
    static const int kConstants[kGameConfigKeyCount] = {
        0x85, 0x01, 0x69, 0xC3A0, 5,  10, 15, 20,     25,     100,  50,   10,
        200,  100,  20,   100,    10, 2,  6,  0x0112, 0x6912, 0x0B, 0x0B, 0x06,
    };
    static const int kNoteDoors[12] = { 50, 180, 260, 350, 450, 640, 765, 810, 828, 846, 864, 882 };
    static const GameConfig::JiggyPuzzle kPuzzles[11] = {
        { 1, 1, 0x5D },  { 2, 2, 0x5E },  { 5, 3, 0x60 },  { 7, 3, 0x63 },  { 8, 4, 0x66 }, { 9, 4, 0x6A },
        { 10, 4, 0x6E }, { 12, 4, 0x72 }, { 15, 4, 0x76 }, { 25, 5, 0x7A }, { 4, 3, 0x7F },
    };
    static const char* kLevelNames[13] = {
        "GAME TOTAL",       "SPIRAL MOUNTAIN",   "GRUNTILDA'S LAIR", "MUMBO'S MOUNTAIN", "TREASURE TROVE COVE",
        "CLANKER'S CAVERN", "BUBBLEGLOOP SWAMP", "FREEZEEZY PEAK",   "GOBI'S VALLEY",    "MAD MONSTER MANSION",
        "RUSTY BUCKET BAY", "CLICK CLOCK WOOD",  "STOP 'N' SWOP",
    };
    // D_8036C560 in pauseMenu.c: the lobby each world returns to, by level index.
    static const GameConfig::ReturnToLair kReturnToLair[11] = {
        { 0x69, 0x2 }, { 0x6D, 0x4 }, { 0x70, 0x2 }, { 0x72, 0x2 }, { 0x6F, 0x6 },  { -1, -1 },
        { 0x6E, 0x3 }, { 0x79, 0x6 }, { 0x77, 0x2 }, { 0x75, 0x2 }, { 0x69, 0x12 },
    };
    for (int key = 0; key < kGameConfigKeyCount; ++key) {
        cfg.constants[key] = kConstants[key];
    }
    for (int door = 0; door < 12; ++door) {
        cfg.noteDoors[door] = kNoteDoors[door];
    }
    for (int puzzle = 0; puzzle < 11; ++puzzle) {
        cfg.puzzles[puzzle] = kPuzzles[puzzle];
        cfg.returnToLair[puzzle] = kReturnToLair[puzzle];
    }
    for (int name = 0; name < 13; ++name) {
        cfg.levelNames[name] = kLevelNames[name];
    }
    return cfg;
}

bool LoadO2rGameConfig(GameConfig& out) {
    out = VanillaGameConfig();
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    auto archives = resources ? resources->GetArchiveManager() : nullptr;
    auto file = archives ? archives->LoadFile("assets/aGameConfig") : nullptr;
    if (!file || !file->IsLoaded || !file->Buffer) {
        return false;
    }
    Reader r{ (const uint8_t*)file->Buffer->data(), file->Buffer->size() };
    if (r.readU32() != kMagic) {
        return false;
    }
    r.readU16(); // version
    const uint16_t sectionCount = r.readU16();
    out.romName = r.str(r.readU8());

    for (uint16_t section = 0; section < sectionCount && r.has(4); ++section) {
        const uint16_t type = r.readU16();
        const uint16_t count = r.readU16();
        switch (type) {
            case 1: // CODE_CONSTANTS
                for (uint16_t e = 0; e < count && r.has(4); ++e) {
                    const uint16_t key = r.readU16();
                    const uint16_t value = r.readU16();
                    if (key < kGameConfigKeyCount) {
                        out.constants[key] = value;
                        out.constantSet[key] = true;
                    }
                }
                break;
            case 2: // SCENE_REMAP
                for (uint16_t e = 0; e < count && r.has(4); ++e) {
                    const int map = r.readU16();
                    out.sceneRemap[map] = r.readU16();
                }
                break;
            case 3: // RETURN_TO_LAIR
                for (uint16_t e = 0; e < count && r.has(6); ++e) {
                    const int index = r.readU8();
                    r.readU8();
                    const int map = r.readU16();
                    const int exit = r.readU16();
                    if (index < 11) {
                        out.returnToLair[index] = { map, exit };
                    }
                }
                break;
            case 4: // MUSIC
                for (uint16_t e = 0; e < count && r.has(6); ++e) {
                    const int map = r.readU16();
                    GameConfig::MusicRow& row = out.music[map];
                    row.track1 = r.readU16();
                    row.track2 = r.readU16();
                }
                break;
            case 5: // SKYBOX
                for (uint16_t e = 0; e < count && r.has(32); ++e) {
                    const int map = r.readU16();
                    GameConfig::SkyRow& row = out.skybox[map];
                    for (int layer = 0; layer < 3; ++layer) {
                        row.models[layer] = (int16_t)r.readU16();
                        row.scales[layer] = r.readF32();
                        row.rotations[layer] = r.readF32();
                    }
                }
                break;
            case 6: // SCENE_DEF
                for (uint16_t e = 0; e < count && r.has(22); ++e) {
                    const int map = r.readU16();
                    GameConfig::SceneDef& row = out.sceneDefs[map];
                    row.opa = (int16_t)r.readU16();
                    row.xlu = (int16_t)r.readU16();
                    for (int axis = 0; axis < 3; ++axis) {
                        row.cubeMin[axis] = (int16_t)r.readU16();
                    }
                    for (int axis = 0; axis < 3; ++axis) {
                        row.cubeMax[axis] = (int16_t)r.readU16();
                    }
                    row.scale = r.readF32();
                }
                break;
            case 7: // NOTE_DOORS
                for (uint16_t e = 0; e < count && r.has(4); ++e) {
                    const int index = r.readU8();
                    r.readU8();
                    const int threshold = r.readU16();
                    if (index < 12) {
                        out.noteDoors[index] = threshold;
                    }
                }
                break;
            case 8: // JIGGY_PUZZLES
                for (uint16_t e = 0; e < count && r.has(6); ++e) {
                    const int index = r.readU8();
                    const int cost = r.readU8();
                    const int size = r.readU8();
                    r.readU8();
                    const int flag = r.readU16();
                    if (index < 11) {
                        out.puzzles[index] = { cost, size, flag };
                    }
                }
                break;
            case 9: // LEVEL_NAMES
                for (uint16_t e = 0; e < count && r.has(2); ++e) {
                    const int index = r.readU8();
                    const std::string name = r.str(r.readU8());
                    if (index < 13) {
                        out.levelNames[index] = name;
                    }
                }
                break;
            case 10: // WARP_DESTINATIONS
                for (uint16_t e = 0; e < count && r.has(4); ++e) {
                    const int warp = r.readU16();
                    out.warpDests[warp] = r.readU16();
                }
                break;
            case 11: // CUSTOM_CODE
                if (count >= 1 && r.has(24)) {
                    out.customCodeRamBase = r.readU32();
                    out.customCodeSha1 = r.hex(20);
                    r.skip((size_t)(count - 1) * 24);
                } else {
                    r.skip((size_t)count * 24);
                }
                break;
            case 12: // ROM_HASH
                if (count >= 1 && r.has(20)) {
                    out.romSha1 = r.hex(20);
                    r.skip((size_t)(count - 1) * 20);
                } else {
                    r.skip((size_t)count * 20);
                }
                break;
            case 13: // CUSTOM_CODE_INFO
                if (count >= 1 && r.has(4)) {
                    out.customCodeKind = r.readU16();
                    r.readU16();
                    r.skip((size_t)(count - 1) * 4);
                } else {
                    r.skip((size_t)count * 4);
                }
                break;
            default:
                // A section this build doesn't know has no length up front; keep what was read.
                out.fromArchive = true;
                return true;
        }
    }
    out.fromArchive = true;
    return true;
}

} // namespace Lightbulb
