#include "O2rImport.h"

extern "C" {
#include "model.h"
}

#include "port/Resource/Type/Sprite.h"
#include <cstring>
#include <fast/Fast3dGui.h>
#include <fast/resource/type/Texture.h>
#include <libultraship/libultra/gbi.h>
#include <map>
#include <memory>
#include <set>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <string>

namespace Lightbulb {
namespace {
std::map<uint32_t, std::string>& spriteIndex() {
    static std::map<uint32_t, std::string> index;
    return index;
}

void ensureIndex() {
    auto& index = spriteIndex();
    if (!index.empty()) {
        return;
    }
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resources) {
        return;
    }
    auto archives = resources->GetArchiveManager();
    if (!archives) {
        return;
    }
    auto files = archives->ListFiles("assets/sprite/*");
    if (!files) {
        return;
    }
    const std::set<std::string> all(files->begin(), files->end());
    for (const std::string& path : *files) {
        if (!all.count(path + "_0_0") && !all.count(path + "_0_1") && !all.count(path + "_0_TLUT")) {
            continue;
        }
        const size_t slash = path.find_last_of('/');
        const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
        if (std::strncmp(name, "ASSET_", 6) == 0) {
            index.emplace(static_cast<uint32_t>(std::strtoul(name + 6, nullptr, 16)), path);
        }
    }
}

bool mapFmt(Fast::TextureType type, int& fmt, int& siz, int& colors) {
    colors = 0;
    switch (type) {
        case Fast::TextureType::RGBA16bpp:
            fmt = G_IM_FMT_RGBA;
            siz = G_IM_SIZ_16b;
            return true;
        case Fast::TextureType::RGBA32bpp:
            fmt = G_IM_FMT_RGBA;
            siz = G_IM_SIZ_32b;
            return true;
        case Fast::TextureType::Palette4bpp:
            fmt = G_IM_FMT_CI;
            siz = G_IM_SIZ_4b;
            colors = 16;
            return true;
        case Fast::TextureType::Palette8bpp:
            fmt = G_IM_FMT_CI;
            siz = G_IM_SIZ_8b;
            colors = 256;
            return true;
        case Fast::TextureType::Grayscale4bpp:
            fmt = G_IM_FMT_I;
            siz = G_IM_SIZ_4b;
            return true;
        case Fast::TextureType::Grayscale8bpp:
            fmt = G_IM_FMT_I;
            siz = G_IM_SIZ_8b;
            return true;
        case Fast::TextureType::GrayscaleAlpha4bpp:
            fmt = G_IM_FMT_IA;
            siz = G_IM_SIZ_4b;
            return true;
        case Fast::TextureType::GrayscaleAlpha8bpp:
            fmt = G_IM_FMT_IA;
            siz = G_IM_SIZ_8b;
            return true;
        case Fast::TextureType::GrayscaleAlpha16bpp:
            fmt = G_IM_FMT_IA;
            siz = G_IM_SIZ_16b;
            return true;
        default:
            return false;
    }
}

struct CachedSprite {
    O2rSpriteTex data;
    std::shared_ptr<Factories::Sprite> keepAlive;
};

std::map<uint32_t, CachedSprite>& spriteCache() {
    static std::map<uint32_t, CachedSprite> cache;
    return cache;
}
} // namespace

void ResetSpriteCache() {
    spriteCache().clear();
}

namespace {

bool adapt(const std::string& base, O2rSpriteTex& out, std::shared_ptr<Factories::Sprite>& keepAlive) {
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resources) {
        return false;
    }
    auto sprite = std::static_pointer_cast<Factories::Sprite>(resources->LoadResource(base));
    if (!sprite || sprite->frames.empty()) {
        return false;
    }

    out = O2rSpriteTex{};
    out.dispW = static_cast<float>(sprite->headerUnk8);
    out.dispH = static_cast<float>(sprite->headerUnkA);
    out.animSpeed = sprite->animSpeed;
    out.animType = sprite->animType;
    out.animDir = sprite->animDirection;
    out.animFlip = sprite->animFlip;

    for (const auto& f : sprite->frames) {
        O2rSpriteFrame frame;
        frame.originX = f.frameHeader.unk0;
        frame.originY = f.frameHeader.unk2;
        frame.frameW = f.frameHeader.w > 0 ? f.frameHeader.w : 1;
        frame.frameH = f.frameHeader.h > 0 ? f.frameHeader.h : 1;
        for (const auto& c : f.chunks) {
            O2rSpriteChunk chunk;
            chunk.texels = c.textureData.data();
            chunk.width = c.header.w;
            chunk.height = c.header.h;
            chunk.posX = c.header.x;
            chunk.posY = c.header.y;
            const char* path = c.resPath.c_str();
            if (std::strncmp(path, "__OTR__", 7) == 0) {
                path += 7;
            }
            auto tex = std::static_pointer_cast<Fast::Texture>(resources->LoadResource(path));
            if (!tex || !mapFmt(tex->Type, chunk.fmt, chunk.siz, chunk.tlutColors)) {
                continue;
            }
            if (chunk.tlutColors > 0 && !f.paletteData.empty()) {
                chunk.tlut = f.paletteData.data();
            }
            frame.chunks.push_back(chunk);
        }
        out.frames.push_back(std::move(frame));
    }

    keepAlive = sprite;
    out.loaded = !out.frames.empty();
    return out.loaded;
}

} // namespace

bool LoadO2rSprite(uint32_t assetId, O2rSpriteTex& out) {
    auto& cache = spriteCache();
    if (auto cit = cache.find(assetId); cit != cache.end()) {
        out = cit->second.data;
        return out.loaded;
    }
    out = O2rSpriteTex{};
    CachedSprite& cached = cache[assetId];
    ensureIndex();
    auto found = spriteIndex().find(assetId);
    if (found == spriteIndex().end()) {
        return false;
    }
    if (!adapt(found->second, cached.data, cached.keepAlive)) {
        return false;
    }
    out = cached.data;
    return true;
}

bool LoadO2rSpriteByPath(const std::string& basePath, O2rSpriteTex& out) {
    const uint32_t key = 0x80000000u | (std::hash<std::string>{}(basePath)&0x7FFFFFFFu);
    auto& cache = spriteCache();
    if (auto cit = cache.find(key); cit != cache.end()) {
        out = cit->second.data;
        return out.loaded;
    }
    CachedSprite& cached = cache[key];
    if (!adapt(basePath, cached.data, cached.keepAlive)) {
        out = O2rSpriteTex{};
        return false;
    }
    out = cached.data;
    return true;
}

std::vector<std::string> ListO2rSpritePaths() {
    ensureIndex();
    std::vector<std::string> out;
    out.reserve(spriteIndex().size());
    for (const auto& [id, path] : spriteIndex()) {
        out.push_back(path);
    }
    return out;
}

SpriteFrame SpriteFrameAt(const O2rSpriteTex& sprite, double seconds, int phase) {
    const int frameCount = (int)sprite.frames.size();
    if (frameCount <= 1 || sprite.animType == 0 || sprite.animSpeed == 0) {
        return {};
    }
    const int type = sprite.animType;
    const bool shared = (type == 1 || type == 2);
    const int period = (type == 3) ? frameCount : (frameCount - (shared ? 1 : 0)) * 2;
    if (period <= 0) {
        return {};
    }
    const long tick = (long)(seconds * 30.0);
    const long phaseOffset = ((long)phase * period) / 32;
    int step = (int)(((tick / (long)sprite.animSpeed) + phaseOffset) % (long)period);
    int dir;
    switch (sprite.animDir) {
        case 1:
            dir = (phase & 2) ? 1 : 0;
            break;
        case 2:
            dir = 1;
            break;
        default:
            dir = 0;
            break;
    }
    const int flipFromMode = (sprite.animFlip == 1) ? (phase & 1) : (sprite.animFlip == 2) ? 1 : 0;
    int mirror = 0;
    int flip;
    switch (type) {
        case 4:
            mirror = (step >= frameCount) ? 1 : 0;
            flip = (step >= frameCount) ? 1 : 0;
            break;
        case 1:
            flip = (step >= frameCount) ? 1 : 0;
            break;
        case 2:
            mirror = (step >= frameCount) ? 1 : 0;
            flip = flipFromMode;
            break;
        default:
            flip = flipFromMode;
            break;
    }
    if (flip ^ dir ^ mirror) {
        step = period - step;
    }
    step += shared ? dir : -dir;
    step = (step < 0) ? step + frameCount : step % frameCount;
    if (step < 0) {
        step = 0;
    }
    if (step >= frameCount) {
        step = frameCount - 1;
    }
    return { step, flip != 0 };
}

namespace {
std::shared_ptr<Fast::Fast3dGui> fastGui() {
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    return window ? std::dynamic_pointer_cast<Fast::Fast3dGui>(window->GetGui()) : nullptr;
}
} // namespace

// Decoded to RGBA with its palette, so the icon keeps its alpha over the level view.
void LoadO2rGuiTexture(const char* name, const char* texturePath, const char* palettePath) {
    auto gui = fastGui();
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!gui || !resources || !resources->LoadResource(texturePath, true)) {
        return;
    }
    gui->LoadGuiTexture(name, texturePath, palettePath, ImVec4(1, 1, 1, 1));
}

void* O2rGuiTexture(const char* name, float& outWidth, float& outHeight) {
    auto gui = fastGui();
    if (!gui || !gui->HasTextureByName(name)) {
        return nullptr;
    }
    const ImVec2 size = gui->GetTextureSize(name);
    outWidth = size.x;
    outHeight = size.y;
    return (void*)(uintptr_t)gui->GetTextureByName(name);
}

int SpriteRestFrame(const O2rSpriteTex& sprite) {
    int best = 0;
    int bestArea = -1;
    for (int frame = 0; frame < (int)sprite.frames.size(); ++frame) {
        const O2rSpriteFrame& candidate = sprite.frames[frame];
        int area = 0;
        for (const O2rSpriteChunk& chunk : candidate.chunks) {
            area += chunk.width * chunk.height;
        }
        if (area == 0) {
            area = candidate.frameW * candidate.frameH;
        }
        if (area > bestArea) {
            bestArea = area;
            best = frame;
        }
    }
    return best;
}

} // namespace Lightbulb
