#include "O2rImport.h"
#include "enums.h"

extern "C" {
#include "model.h"
}

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/type/Blob.h>
#include <string>
#include <unordered_map>

namespace Lightbulb {
namespace {
constexpr float kDtor = 3.14159265358979323846f / 180.0f;

struct AnimRow {
    uint32_t model;
    uint32_t anim;
    float duration;
};

const AnimRow kModelAnims[] = {
#include "ActorAnims.inc"
};
} // namespace
} // namespace Lightbulb

namespace Lightbulb {
namespace {
void mtxIdentity(Mtx& mtx) {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            mtx.mf[row][col] = (row == col) ? 1.0f : 0.0f;
        }
    }
}
void mtxPreTranslate(Mtx& mtx, float tx, float ty, float tz) {
    for (int axis = 0; axis < 3; ++axis) {
        mtx.mf[3][axis] += mtx.mf[0][axis] * tx + mtx.mf[1][axis] * ty + mtx.mf[2][axis] * tz;
    }
}
void mtxPreScale(Mtx& mtx, float sx, float sy, float sz) {
    for (int axis = 0; axis < 3; ++axis) {
        mtx.mf[0][axis] *= sx;
        mtx.mf[1][axis] *= sy;
        mtx.mf[2][axis] *= sz;
    }
}
void mtxPreRotPitch(Mtx& mtx, float deg) {
    if (deg == 0.0f) {
        return;
    }
    const float rad = deg * kDtor, sinA = std::sin(rad), cosA = std::cos(rad);
    for (int row = 0; row < 3; ++row) {
        const float f1 = mtx.mf[1][row], f2 = mtx.mf[2][row];
        mtx.mf[1][row] = f1 * cosA + f2 * sinA;
        mtx.mf[2][row] = -f1 * sinA + f2 * cosA;
    }
}
void mtxPreRotYaw(Mtx& mtx, float deg) {
    if (deg == 0.0f) {
        return;
    }
    const float rad = deg * kDtor, sinA = std::sin(rad), cosA = std::cos(rad);
    for (int row = 0; row < 3; ++row) {
        const float f0 = mtx.mf[0][row], f2 = mtx.mf[2][row];
        mtx.mf[0][row] = f0 * cosA - f2 * sinA;
        mtx.mf[2][row] = f0 * sinA + f2 * cosA;
    }
}
void mtxPreRotRoll(Mtx& mtx, float deg) {
    if (deg == 0.0f) {
        return;
    }
    const float rad = deg * kDtor, sinA = std::sin(rad), cosA = std::cos(rad);
    for (int row = 0; row < 3; ++row) {
        const float f0 = mtx.mf[0][row], f1 = mtx.mf[1][row];
        mtx.mf[0][row] = f0 * cosA + f1 * sinA;
        mtx.mf[1][row] = -f0 * sinA + f1 * cosA;
    }
}

float catmull4(float time, const float keys[4]) {
    if (time < 0.0f) {
        time = 0.0f;
    }
    if (time > 1.0f) {
        time = 1.0f;
    }
    const float c2 = -0.5f * keys[0] + 1.5f * keys[1] - 1.5f * keys[2] + 0.5f * keys[3];
    const float c1 = 1.0f * keys[0] - 2.5f * keys[1] + 2.0f * keys[2] - 0.5f * keys[3];
    const float c0 = -0.5f * keys[0] + 0.5f * keys[2];
    return (((c2 * time + c1) * time + c0) * time) + keys[1];
}

float sampleChannel(const O2rAnimChannel& channel, float time, int startFrame, float fallback) {
    const std::vector<O2rAnimKey>& keys = channel.keys;
    const int keyCount = (int)keys.size();
    if (keyCount == 0) {
        return fallback;
    }
    const int frame = (int)time;
    if (frame < keys[0].time) {
        float control[4];
        control[0] = control[1] = fallback;
        control[2] = keys[0].val;
        control[3] = (keys[0].smooth == 1 && keyCount >= 2) ? keys[1].val : control[2];
        const float span = (float)(keys[0].time - startFrame);
        return catmull4(span != 0.0f ? (time - startFrame) / span : 0.0f, control);
    }
    if (frame >= keys[keyCount - 1].time) {
        float control[4];
        control[1] = keys[keyCount - 1].val;
        control[0] = (keys[keyCount - 1].smoothNext == 1 && keyCount >= 2) ? keys[keyCount - 2].val : control[1];
        control[2] = control[3] = control[1];
        return catmull4(time - keys[keyCount - 1].time, control);
    }
    int segment = 0;
    for (int keyIdx = 0; keyIdx < keyCount - 1; ++keyIdx) {
        if (keys[keyIdx].time <= frame) {
            segment = keyIdx;
        } else {
            break;
        }
    }
    const O2rAnimKey& from = keys[segment];
    const O2rAnimKey& to = keys[segment + 1];
    const float span = (float)(to.time - from.time);
    const float frac = span != 0.0f ? (time - from.time) / span : 0.0f;
    if (from.smoothNext == 0 && to.smooth == 0) {
        return from.val + (to.val - from.val) * frac;
    }
    float control[4];
    control[1] = from.val;
    control[2] = to.val;
    control[0] = (from.smoothNext == 1 && segment > 0) ? keys[segment - 1].val : control[1];
    control[3] = (to.smooth == 1 && segment + 2 < keyCount) ? keys[segment + 2].val : control[2];
    return catmull4(frac, control);
}

struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0, cursor = 0;
    bool valid = true;
    bool need(size_t bytes) {
        return valid && (valid = (cursor + bytes <= size));
    }
    int16_t readI16() {
        if (!need(2))
            return 0;
        int16_t v;
        std::memcpy(&v, data + cursor, 2);
        cursor += 2;
        return v;
    }
    uint16_t readU16() {
        if (!need(2))
            return 0;
        uint16_t v;
        std::memcpy(&v, data + cursor, 2);
        cursor += 2;
        return v;
    }
    uint32_t readU32() {
        if (!need(4))
            return 0;
        uint32_t v;
        std::memcpy(&v, data + cursor, 4);
        cursor += 4;
        return v;
    }
    uint8_t readU8() {
        return need(1) ? data[cursor++] : 0;
    }
};

std::map<std::string, O2rAnim>& animCache() {
    static std::map<std::string, O2rAnim> cache;
    return cache;
}
} // namespace

void ResetAnimCache() {
    animCache().clear();
}

namespace {}

std::vector<std::string> ListO2rAnimPaths() {
    return ListO2rResourcePaths("anim");
}

bool LoadO2rAnim(const std::string& path, O2rAnim& out) {
    auto& cache = animCache();
    if (auto found = cache.find(path); found != cache.end()) {
        out = found->second;
        return out.loaded;
    }
    out = O2rAnim{};
    O2rAnim& cached = cache[path];

    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resources) {
        return false;
    }
    auto blob = std::static_pointer_cast<Ship::Blob>(resources->LoadResource(path));
    if (!blob || blob->Data.empty()) {
        return false;
    }
    Reader reader{ blob->Data.data(), blob->Data.size(), 0 };

    out.startFrame = reader.readI16();
    out.endFrame = reader.readI16();
    const uint32_t channelCount = static_cast<uint16_t>(reader.readI16());
    reader.readI16();
    out.channels.reserve(channelCount);
    for (uint32_t channelIdx = 0; channelIdx < channelCount && reader.valid; ++channelIdx) {
        O2rAnimChannel channel;
        const uint16_t channelBits = reader.readU16();
        channel.bone = static_cast<int16_t>(channelBits & 0x0FFF);
        channel.channel = static_cast<int16_t>((channelBits >> 12) & 0x000F);
        const uint32_t keyCount = static_cast<uint16_t>(reader.readI16());
        channel.keys.reserve(keyCount);
        for (uint32_t keyIdx = 0; keyIdx < keyCount && reader.valid; ++keyIdx) {
            O2rAnimKey key;
            const uint16_t keyBits = reader.readU16();
            key.smooth = static_cast<uint8_t>(keyBits & 1);
            key.smoothNext = static_cast<uint8_t>((keyBits >> 1) & 1);
            key.time = static_cast<uint16_t>((keyBits >> 2) & 0x3FFF);
            key.val = (float)reader.readI16() / 64.0f;
            channel.keys.push_back(key);
        }
        if (channel.bone > out.maxBoneId) {
            out.maxBoneId = channel.bone;
        }
        out.channels.push_back(std::move(channel));
    }
    out.loaded = reader.valid && !out.channels.empty();
    cached = out;
    return out.loaded;
}

const std::unordered_map<uint64_t, float>& animIndex() {
    static const std::unordered_map<uint64_t, float> index = [] {
        std::unordered_map<uint64_t, float> built;
        built.reserve(sizeof(kModelAnims) / sizeof(kModelAnims[0]));
        for (const AnimRow& row : kModelAnims) {
            built.emplace(((uint64_t)row.model << 32) | row.anim, row.duration);
        }
        return built;
    }();
    return index;
}

const std::set<uint32_t>& modelsWithAnims() {
    static const std::set<uint32_t> models = [] {
        std::set<uint32_t> built;
        for (const AnimRow& row : kModelAnims) {
            built.insert(row.model);
        }
        return built;
    }();
    return models;
}

bool ModelHasAnimTable(uint32_t modelAsset) {
    return modelsWithAnims().count(modelAsset) != 0;
}

bool ModelUsesAnim(uint32_t modelAsset, uint32_t animAsset) {
    return animIndex().count(((uint64_t)modelAsset << 32) | animAsset) != 0;
}

float AnimDuration(uint32_t modelAsset, uint32_t animAsset) {
    const auto found = animIndex().find(((uint64_t)modelAsset << 32) | animAsset);
    return found == animIndex().end() ? 0.0f : found->second;
}

void SampleO2rAnim(const O2rAnim& anim, float progress, std::vector<BonePose>& bonesOut) {
    bonesOut.assign((size_t)anim.maxBoneId + 1, BonePose{});
    if (!anim.loaded) {
        return;
    }
    const float time = (float)anim.startFrame + progress * (float)(anim.endFrame - anim.startFrame);
    for (const O2rAnimChannel& channel : anim.channels) {
        if (channel.bone < 0 || channel.bone > anim.maxBoneId || channel.channel < 0 || channel.channel > 8) {
            continue;
        }
        const bool isScale = channel.channel >= 3 && channel.channel <= 5;
        const float value = sampleChannel(channel, time, anim.startFrame, isScale ? 1.0f : 0.0f);
        BonePose& pose = bonesOut[channel.bone];
        if (channel.channel < 3) {
            pose.rot[channel.channel] = value;
        } else if (isScale) {
            pose.scale[channel.channel - 3] = value;
        } else {
            pose.trans[channel.channel - 6] = value;
        }
    }
}

int BuildBoneMatrices(BKModelBin* model, const std::vector<BonePose>& bones, Mtx* out, int maxOut) {
    if (!model || model->animation_list_offset == 0) {
        return 0;
    }
    BKAnimationList* animList = (BKAnimationList*)((uint8_t*)model + model->animation_list_offset);
    int count = animList->count;
    if (count <= 0) {
        return 0;
    }
    if (count > maxOut) {
        count = maxOut;
    }
    const float transScale = animList->unk0;
    const BonePose identity{};
    for (int boneIdx = 0; boneIdx < count; ++boneIdx) {
        const BKAnimation& animation = animList->animations[boneIdx];
        const BonePose& pose =
            (animation.bone_id >= 0 && (size_t)animation.bone_id < bones.size()) ? bones[animation.bone_id] : identity;
        Mtx mtx;
        if (animation.mtx_id >= 0 && animation.mtx_id < boneIdx) {
            std::memcpy(&mtx, &out[animation.mtx_id], sizeof(Mtx));
        } else {
            mtxIdentity(mtx);
        }
        const float* rest = animation.translation;
        mtxPreTranslate(mtx, rest[0] + transScale * pose.trans[0], rest[1] + transScale * pose.trans[1],
                        rest[2] + transScale * pose.trans[2]);
        mtxPreRotRoll(mtx, pose.rot[2]);
        mtxPreRotYaw(mtx, pose.rot[1]);
        mtxPreRotPitch(mtx, pose.rot[0]);
        mtxPreScale(mtx, pose.scale[0], pose.scale[1], pose.scale[2]);
        mtxPreTranslate(mtx, -rest[0], -rest[1], -rest[2]);
        std::memcpy(&out[boneIdx], &mtx, sizeof(Mtx));
    }
    return count;
}

void TransformAnimVertices(BKModelBin* model, const Mtx* boneMtx, int boneCount) {
    if (!model || model->anim_vertices_list_offset == 0 || model->vtx_list_offset == 0) {
        return;
    }
    BKAnimVerticesList* animVerts = (BKAnimVerticesList*)((uint8_t*)model + model->anim_vertices_list_offset);
    Vtx* verts = modelbin_getVtxList(model)->vertices;

    Mtx ident;
    mtxIdentity(ident);
    const uint8_t* cursor = animVerts->data;
    for (int group = 0; group < animVerts->count; ++group) {
        const BKAnimVertices* vtxGroup = (const BKAnimVertices*)cursor;
        const Mtx& mtx = (boneMtx && vtxGroup->anim_index >= 0 && vtxGroup->anim_index < boneCount)
                             ? boneMtx[vtxGroup->anim_index]
                             : ident;
        float out[3];
        for (int axis = 0; axis < 3; ++axis) {
            out[axis] = (float)vtxGroup->coord[0] * mtx.mf[0][axis] + (float)vtxGroup->coord[1] * mtx.mf[1][axis] +
                        (float)vtxGroup->coord[2] * mtx.mf[2][axis] + mtx.mf[3][axis];
        }
        for (int slot = 0; slot < vtxGroup->vtx_count; ++slot) {
            Vtx& vert = verts[vtxGroup->vtx_list[slot]];
            vert.v.ob[0] = out[0];
            vert.v.ob[1] = out[1];
            vert.v.ob[2] = out[2];
        }
        cursor = (const uint8_t*)((const int16_t*)(vtxGroup + 1) + (vtxGroup->vtx_count - 1));
    }
}

} // namespace Lightbulb
