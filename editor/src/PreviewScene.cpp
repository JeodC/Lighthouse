#include "PreviewScene.h"
#include "O2rImport.h"
#include "imgui.h"
#include <cstdio>
#include <fast/Fast3dWindow.h>
#include <fast/backends/gfx_rendering_api.h>
#include <fast/interpreter.h>
#include <memory>
#include <ship/Context.h>

extern "C" {
#include "model.h"
}

#include <fast/ucodehandlers.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

extern "C" {
void modelRender_init(void);
void modelRender_reset(void);
void modelRender_setEnvColor(s32 r, s32 g, s32 b, s32 a);
void modelRender_func_8033A25C(int enableFrustumCull);
void modelRender_setDepthMode(int mode);
void modelRender_setAnimatedTexturesCacheId(int cacheId);
void modelRender_func_8033A470(int id, int mask);
void* modelRender_draw(void** gfx, void** mtx, float pos[3], float rot[3], float scale, float* a5, void* model);
void AnimTextureListCache_init(void);
int AnimTextureListCache_newList(void);
void AnimTextureListCache_setAnimTextureList(int index, void* bkAnimTextureList);
void AnimTextureListCache_update(void);
void lh_setFrameDelta(float deltaSeconds);
void func_8034C97C(void);
void func_8034C8D8(void);
void func_8034C6DC(void* bkModel);
void func_8034C9D4(void);
void lh_setEditorBones(const float* mats, int count);
}

enum { MODEL_RENDER_DEPTH_NONE = 0, MODEL_RENDER_DEPTH_FULL = 1, MODEL_RENDER_DEPTH_COMPARE = 2 };

namespace Lightbulb {
namespace {
Gfx sGfx[131072];
Mtx sMtx[8192];
Vp sVp;
Vtx sVtx[8192];
Vtx sBackdropVtx[4];

LookAt sEnvLookAt;

constexpr float kPi = 3.14159265358979323846f;
constexpr size_t kGfxSoftLimit = 120000;
constexpr size_t kMtxSoftLimit = 7900;
inline float rad(float deg) {
    return deg * (kPi / 180.0f);
}

struct Frustum {
    float planes[6][4];
    bool valid = false;

    bool cullSphere(const float center[3], float radius) const {
        if (!valid) {
            return false;
        }
        for (int plane = 0; plane < 6; ++plane) {
            const float dist = planes[plane][0] * center[0] + planes[plane][1] * center[1] +
                               planes[plane][2] * center[2] + planes[plane][3];
            if (dist < -radius) {
                return true;
            }
        }
        return false;
    }
};

inline float dot3(const float lhs[3], const float rhs[3]) {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}
inline void cross3(float out[3], const float lhs[3], const float rhs[3]) {
    out[0] = lhs[1] * rhs[2] - lhs[2] * rhs[1];
    out[1] = lhs[2] * rhs[0] - lhs[0] * rhs[2];
    out[2] = lhs[0] * rhs[1] - lhs[1] * rhs[0];
}
inline void normalize3(float vec[3]) {
    const float len = std::sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
    if (len > 1e-8f) {
        vec[0] /= len;
        vec[1] /= len;
        vec[2] /= len;
    }
}

void setPerspectiveMtx(Mtx* mtx, uint16_t* perspNorm, float fovy, float aspect, float nearp, float farp) {
    if (nearp < 1.0f)
        nearp = 1.0f;
    if (farp < nearp + 100.0f)
        farp = nearp + 100.0f;
    float(*cell)[4] = mtx->mf;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++)
            cell[row][col] = (row == col) ? 1.0f : 0.0f;
    const float fovRad = rad(fovy);
    const float cotHalfFov = std::cos(fovRad * 0.5f) / std::sin(fovRad * 0.5f);
    cell[0][0] = cotHalfFov / aspect;
    cell[1][1] = cotHalfFov;
    cell[2][2] = (nearp + farp) / (nearp - farp);
    cell[2][3] = -1.0f;
    cell[3][2] = (2.0f * nearp * farp) / (nearp - farp);
    cell[3][3] = 0.0f;
    if (perspNorm) {
        if (nearp + farp <= 2.0f) {
            *perspNorm = 0xFFFF;
        } else {
            *perspNorm = (uint16_t)((2.0f * 65536.0f) / (nearp + farp));
            if (*perspNorm == 0)
                *perspNorm = 1;
        }
    }
}

void setLookAtMtx(Mtx* mtx, const float eye[3], const float at[3], const float up[3]) {
    float forward[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
    normalize3(forward);
    float side[3];
    cross3(side, forward, up);
    normalize3(side);
    float upVec[3];
    cross3(upVec, side, forward);
    float(*cell)[4] = mtx->mf;
    cell[0][0] = side[0];
    cell[0][1] = upVec[0];
    cell[0][2] = -forward[0];
    cell[0][3] = 0.0f;
    cell[1][0] = side[1];
    cell[1][1] = upVec[1];
    cell[1][2] = -forward[1];
    cell[1][3] = 0.0f;
    cell[2][0] = side[2];
    cell[2][1] = upVec[2];
    cell[2][2] = -forward[2];
    cell[2][3] = 0.0f;
    cell[3][0] = -dot3(side, eye);
    cell[3][1] = -dot3(upVec, eye);
    cell[3][2] = dot3(forward, eye);
    cell[3][3] = 1.0f;
}

void setObjMtx(Mtx* mtx, const float pos[3], const float rotDeg[3], float scale) {
    const float cosPitch = std::cos(rad(rotDeg[0])), sinPitch = std::sin(rad(rotDeg[0]));
    const float cosYaw = std::cos(rad(rotDeg[1])), sinYaw = std::sin(rad(rotDeg[1]));
    const float cosRoll = std::cos(rad(rotDeg[2])), sinRoll = std::sin(rad(rotDeg[2]));
    const float pitchMtx[3][3] = { { 1, 0, 0 }, { 0, cosPitch, sinPitch }, { 0, -sinPitch, cosPitch } };
    const float yawMtx[3][3] = { { cosYaw, 0, -sinYaw }, { 0, 1, 0 }, { sinYaw, 0, cosYaw } };
    const float rollMtx[3][3] = { { cosRoll, sinRoll, 0 }, { -sinRoll, cosRoll, 0 }, { 0, 0, 1 } };
    auto mul3x3 = [](const float lhs[3][3], const float rhs[3][3], float out[3][3]) {
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++)
                out[row][col] = lhs[row][0] * rhs[0][col] + lhs[row][1] * rhs[1][col] + lhs[row][2] * rhs[2][col];
    };
    float partial[3][3], rot[3][3];
    mul3x3(pitchMtx, yawMtx, partial);
    mul3x3(partial, rollMtx, rot);
    float(*cell)[4] = mtx->mf;
    for (int row = 0; row < 3; row++) {
        cell[row][0] = rot[row][0] * scale;
        cell[row][1] = rot[row][1] * scale;
        cell[row][2] = rot[row][2] * scale;
        cell[row][3] = 0.0f;
    }
    cell[3][0] = pos[0];
    cell[3][1] = pos[1];
    cell[3][2] = pos[2];
    cell[3][3] = 1.0f;
}

void gizmoRotationMtx(const float pyrDeg[3], float m[3][3]) {
    const float cp = std::cos(rad(pyrDeg[0])), sp = std::sin(rad(pyrDeg[0]));
    const float cy = std::cos(rad(pyrDeg[1])), sy = std::sin(rad(pyrDeg[1]));
    const float cr = std::cos(rad(pyrDeg[2])), sr = std::sin(rad(pyrDeg[2]));
    m[0][0] = cr * cy;
    m[0][1] = cr * sy * sp - sr * cp;
    m[0][2] = cr * sy * cp + sr * sp;
    m[1][0] = sr * cy;
    m[1][1] = sr * sy * sp + cr * cp;
    m[1][2] = sr * sy * cp - cr * sp;
    m[2][0] = -sy;
    m[2][1] = cy * sp;
    m[2][2] = cy * cp;
}

constexpr float kCubeHalf = 25.0f;
constexpr float kPyramidApexY = 30.0f;
const float kCameraPoints[8][3] = {
    { -50, -50, 0 }, { 50, -50, 0 }, { -50, 50, 0 }, { 50, 50, 0 },
    { 0, 0, 50 },    { -40, 55, 0 }, { 40, 55, 0 },  { 0, 85, 0 },
};
const uint8_t kBoxEdges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 },
                                   { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

// Corner bit 0 picks x, bit 1 y, bit 2 z.
void boxEdges(const float lo[3], const float hi[3], float edges[12][2][3]) {
    for (int edge = 0; edge < 12; ++edge) {
        for (int end = 0; end < 2; ++end) {
            const int corner = kBoxEdges[edge][end];
            edges[edge][end][0] = (corner & 1) ? hi[0] : lo[0];
            edges[edge][end][1] = (corner & 2) ? hi[1] : lo[1];
            edges[edge][end][2] = (corner & 4) ? hi[2] : lo[2];
        }
    }
}

void drawBillboard(Gfx*& gfx, Vtx* verts, const SpriteBillboard& sprite, const float right[3], const float up[3]) {
    const float* origin = sprite.pos;
    const short texW = (short)(sprite.width << 5), texH = (short)(sprite.height << 5);
    auto setVert = [&](Vtx* vert, float localX, float localY, short texS, short texT) {
        vert->v.ob[0] = origin[0] + right[0] * localX + up[0] * localY;
        vert->v.ob[1] = origin[1] + right[1] * localX + up[1] * localY;
        vert->v.ob[2] = origin[2] + right[2] * localX + up[2] * localY;
        vert->v.flag = 0;
        vert->v.tc[0] = texS;
        vert->v.tc[1] = texT;
        vert->v.cn[0] = 255;
        vert->v.cn[1] = 255;
        vert->v.cn[2] = 255;
        vert->v.cn[3] = 255;
    };
    const float mirrorX = sprite.mirror ? -1.0f : 1.0f;
    setVert(&verts[0], mirrorX * sprite.x0, sprite.y1, 0, 0);
    setVert(&verts[1], mirrorX * sprite.x1, sprite.y1, texW, 0);
    setVert(&verts[2], mirrorX * sprite.x0, sprite.y0, 0, texH);
    setVert(&verts[3], mirrorX * sprite.x1, sprite.y0, texW, texH);

    gDPPipeSync(gfx++);
    gSPClearGeometryMode(gfx++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_BOTH);
    gSPSetGeometryMode(gfx++, G_ZBUFFER);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);
    gDPSetCombineMode(gfx++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetRenderMode(gfx++, G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureLUT(gfx++, sprite.tlut ? G_TT_RGBA16 : G_TT_NONE);
    gSPSegment(gfx++, 0x04, (void*)verts);
    gSPSegment(gfx++, 0x05, (void*)sprite.texels);
    if (sprite.tlut) {
        gSPSegment(gfx++, 0x06, (void*)sprite.tlut);
        if (sprite.tlutColors <= 16) {
            gDPLoadTLUT_pal16(gfx++, 0, (void*)(uintptr_t)0x06000001);
        } else {
            gDPLoadTLUT_pal256(gfx++, (void*)(uintptr_t)0x06000001);
        }
    }
    void* texImage = (void*)(uintptr_t)0x05000001;
    const int fmt = sprite.fmt, texWidth = sprite.width, texHeight = sprite.height;
    switch (sprite.siz) {
        case G_IM_SIZ_4b:
            gDPLoadTextureBlock_4b(gfx++, texImage, fmt, texWidth, texHeight, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK,
                                   G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case G_IM_SIZ_8b:
            gDPLoadTextureBlock(gfx++, texImage, fmt, G_IM_SIZ_8b, texWidth, texHeight, 0, G_TX_CLAMP, G_TX_CLAMP,
                                G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case G_IM_SIZ_32b:
            gDPLoadTextureBlock(gfx++, texImage, fmt, G_IM_SIZ_32b, texWidth, texHeight, 0, G_TX_CLAMP, G_TX_CLAMP,
                                G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        default:
            gDPLoadTextureBlock(gfx++, texImage, fmt, G_IM_SIZ_16b, texWidth, texHeight, 0, G_TX_CLAMP, G_TX_CLAMP,
                                G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
    }
    __gSPVertex(gfx++, (void*)(uintptr_t)0x04000001, 4, 0);
    if (sprite.mirror) {
        gSP2Triangles(gfx++, 0, 2, 1, 0, 1, 2, 3, 0);
    } else {
        gSP2Triangles(gfx++, 0, 1, 2, 0, 1, 3, 2, 0);
    }
}

void buildFrustum(Frustum& frustum, const float eye[3], const float focus[3], float fovYDeg, float aspect, float nearp,
                  float farp) {
    float fwd[3] = { focus[0] - eye[0], focus[1] - eye[1], focus[2] - eye[2] };
    if (dot3(fwd, fwd) <= 1e-8f) {
        frustum.valid = false;
        return;
    }
    normalize3(fwd);
    const float wup[3] = { 0.0f, 1.0f, 0.0f };
    float right[3];
    cross3(right, fwd, wup);
    if (dot3(right, right) <= 1e-8f) {
        frustum.valid = false;
        return;
    }
    normalize3(right);
    float up[3];
    cross3(up, right, fwd);

    const float tanHalfY = std::tan(rad(fovYDeg) * 0.5f);
    const float tanHalfX = tanHalfY * aspect;
    auto setPlane = [&](int idx, const float axis[3], const float point[3]) {
        float normal[3] = { axis[0], axis[1], axis[2] };
        normalize3(normal);
        for (int axisIdx = 0; axisIdx < 3; ++axisIdx) {
            frustum.planes[idx][axisIdx] = normal[axisIdx];
        }
        frustum.planes[idx][3] = -dot3(normal, point);
    };
    float nearCenter[3], farCenter[3], back[3];
    for (int axisIdx = 0; axisIdx < 3; ++axisIdx) {
        nearCenter[axisIdx] = eye[axisIdx] + fwd[axisIdx] * nearp;
        farCenter[axisIdx] = eye[axisIdx] + fwd[axisIdx] * farp;
        back[axisIdx] = -fwd[axisIdx];
    }
    setPlane(0, fwd, nearCenter);
    setPlane(1, back, farCenter);

    // Each side plane hinges on one axis and follows the frustum edge along the other.
    float edge[3], normal[3];
    auto sideEdge = [&](const float along[3], float spread) {
        for (int axisIdx = 0; axisIdx < 3; ++axisIdx) {
            edge[axisIdx] = fwd[axisIdx] + along[axisIdx] * spread;
        }
    };
    sideEdge(right, tanHalfX);
    cross3(normal, up, edge);
    setPlane(2, normal, eye);
    sideEdge(right, -tanHalfX);
    cross3(normal, edge, up);
    setPlane(3, normal, eye);
    sideEdge(up, tanHalfY);
    cross3(normal, edge, right);
    setPlane(4, normal, eye);
    sideEdge(up, -tanHalfY);
    cross3(normal, right, edge);
    setPlane(5, normal, eye);
    frustum.valid = true;
}

bool instanceCulled(const Frustum& frustum, BKModelBin* model, const float pos[3], float scale) {
    BKVertexList* vertexList = model->vtx_list_offset ? modelbin_getVtxList(model) : nullptr;
    if (!vertexList) {
        return false;
    }
    const float safeScale = (scale > 0.0f) ? scale : 1.0f;
    const float center[3] = { pos[0] + (float)vertexList->centerCoord[0] * safeScale,
                              pos[1] + (float)vertexList->centerCoord[1] * safeScale,
                              pos[2] + (float)vertexList->centerCoord[2] * safeScale };
    const float radius = (float)vertexList->local_norm * safeScale + 1.0f;
    return frustum.cullSphere(center, radius);
}

// Segment 3 holds the render-mode table, so a G_DL into it selects the pass.
// Indices 0, 1, 6 and 7 are the opaque entries; anything else is translucent.
bool modelIsPureXlu(BKModelBin* model) {
    if (!model || model->gfx_list_offset == 0) {
        return false;
    }
    BKGfxList* gfxList = modelbin_getGfxList(model);
    bool anyXlu = false, anyOpa = false;
    for (uint32_t cmd = 0; cmd < gfxList->size; ++cmd) {
        const uint32_t w0 = (uint32_t)gfxList->list[cmd].words.w0;
        const uint32_t w1 = (uint32_t)gfxList->list[cmd].words.w1;
        if ((w0 >> 24) != 0x06 || ((w1 >> 24) & 0xFF) != 0x03) {
            continue;
        }
        const uint32_t idx = (w1 & 0x00FFFFFF) / 32;
        if (idx == 0 || idx == 1 || idx == 6 || idx == 7) {
            anyOpa = true;
        } else {
            anyXlu = true;
        }
    }
    return anyXlu && !anyOpa;
}

struct DrawnBounds {
    float min[3] = { 0, 0, 0 };
    float max[3] = { 0, 0, 0 };
    bool valid = false;
};

std::vector<DrawnBounds> sInstanceBounds;

// Replays the matrix and vertex commands modelRender_draw just emitted, so bounds
// come from posed geometry: a skeletal actor's vertex list spans every bind pose.
struct BoundsWalk {
    uintptr_t segments[16] = {};
    float modelView[12][4][4];
    int depth = 1;
    int budget = 1 << 18;
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    float hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool any = false;

    BoundsWalk() {
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                modelView[0][row][col] = (row == col) ? 1.0f : 0.0f;
            }
        }
    }

    const void* resolve(uintptr_t word) const {
        if (!(word & 1)) {
            return (const void*)word;
        }
        const uintptr_t base = segments[(word >> 24) & 0xF];
        return base ? (const void*)(base + (word & 0x00FFFFFE)) : nullptr;
    }

    void applyMtx(uint8_t params, const Mtx* mtx) {
        if (!mtx || (params & G_MTX_PROJECTION)) {
            return;
        }
        if ((params & G_MTX_PUSH) && depth < 12) {
            std::memcpy(modelView[depth], modelView[depth - 1], sizeof(modelView[0]));
            ++depth;
        }
        float(*top)[4] = modelView[depth - 1];
        if (params & G_MTX_LOAD) {
            std::memcpy(top, mtx->mf, sizeof(modelView[0]));
            return;
        }
        float product[4][4];
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                product[row][col] = mtx->mf[row][0] * top[0][col] + mtx->mf[row][1] * top[1][col] +
                                    mtx->mf[row][2] * top[2][col] + mtx->mf[row][3] * top[3][col];
            }
        }
        std::memcpy(top, product, sizeof(product));
    }

    void addVertices(const Vtx* verts, int count) {
        if (!verts || count <= 0) {
            return;
        }
        const float(*mtx)[4] = modelView[depth - 1];
        for (int vert = 0; vert < count; ++vert) {
            const float* ob = verts[vert].v.ob;
            for (int axis = 0; axis < 3; ++axis) {
                const float value = ob[0] * mtx[0][axis] + ob[1] * mtx[1][axis] + ob[2] * mtx[2][axis] + mtx[3][axis];
                if (value < lo[axis]) {
                    lo[axis] = value;
                }
                if (value > hi[axis]) {
                    hi[axis] = value;
                }
            }
        }
        any = true;
    }

    void run(const Gfx* cmd, const Gfx* end, int nesting) {
        while (cmd && (!end || cmd < end) && --budget > 0) {
            const uintptr_t w0 = cmd->words.w0;
            const uintptr_t w1 = cmd->words.w1;
            switch ((uint8_t)(w0 >> 24)) {
                case (uint8_t)G_VTX:
                    addVertices((const Vtx*)resolve(w1), (int)((w0 >> 10) & 0x3F));
                    break;
                case (uint8_t)G_MTX:
                    applyMtx((uint8_t)(w0 >> 16), (const Mtx*)resolve(w1));
                    break;
                case (uint8_t)G_POPMTX:
                    if (depth > 1) {
                        --depth;
                    }
                    break;
                case (uint8_t)G_MOVEWORD:
                    if ((uint8_t)w0 == G_MW_SEGMENT) {
                        segments[((w0 >> 8) & 0x3F) / 4] = w1;
                    }
                    break;
                case (uint8_t)G_DL: {
                    const Gfx* sub = (const Gfx*)resolve(w1);
                    if (!sub) {
                        break;
                    }
                    if ((w0 >> 16) & 1) {
                        cmd = sub;
                        continue;
                    }
                    if (nesting < 8) {
                        run(sub, nullptr, nesting + 1);
                    }
                    break;
                }
                case (uint8_t)G_ENDDL:
                    return;
                default:
                    break;
            }
            ++cmd;
        }
    }

    void store(DrawnBounds& out) const {
        out.valid = any;
        for (int axis = 0; any && axis < 3; ++axis) {
            out.min[axis] = lo[axis];
            out.max[axis] = hi[axis];
        }
    }
};

int animCacheFor(BKModelBin* model) {
    static std::unordered_map<BKModelBin*, int> sCacheIds;
    if (!model || model->animated_texture_list_offset == 0) {
        return 0;
    }
    auto it = sCacheIds.find(model);
    if (it != sCacheIds.end()) {
        return it->second;
    }
    int id = AnimTextureListCache_newList();
    AnimTextureListCache_setAnimTextureList(id, modelbin_getAnimTextureList(model));
    sCacheIds.emplace(model, id);
    return id;
}

std::unordered_set<BKModelBin*>& registeredMapXforms() {
    static std::unordered_set<BKModelBin*> registered;
    return registered;
}

void registerMapXforms(BKModelBin* model) {
    if (!model || model->mesh_list_offset == 0 || model->vtx_list_offset == 0) {
        return;
    }
    if (!registeredMapXforms().insert(model).second) {
        return;
    }
    void* meshList = (void*)((uint8_t*)model + model->mesh_list_offset);
    BKModel* bk = meshList_createModel((BKMeshList*)meshList, modelbin_getVtxList(model));
    if (bk) {
        func_8034C6DC(bk);
    }
}

std::shared_ptr<Fast::Interpreter> fastInterpreter() {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    return window ? window->GetInterpreterWeak().lock() : nullptr;
}

int previewFramebuffer(Fast::Interpreter& interpreter, int bank, int viewId, int width, int height) {
    struct Cached {
        int id = -1, width = 0, height = 0;
    };
    static std::unordered_map<int, Cached> sCache;
    Cached& entry = sCache[(bank << 24) | (viewId & 0xFFFFFF)];
    if (entry.id < 0 || entry.width != width || entry.height != height) {
        entry.id = interpreter.CreateFrameBuffer((uint32_t)width, (uint32_t)height, (uint32_t)width, (uint32_t)height,
                                                 0, false);
        entry.width = width;
        entry.height = height;
    }
    return entry.id;
}

void* runToFramebuffer(Fast::Interpreter& interpreter, Gfx* displayList, int framebuffer) {
    auto* renderApi = interpreter.GetCurrentRenderingAPI();
    renderApi->StartDrawToFramebuffer(framebuffer, 1.0f);
    renderApi->ClearFramebuffer(true, true);
    std::unordered_map<Mtx*, MtxF> noReplacements;
    interpreter.Run(displayList, noReplacements);
    return renderApi->GetFramebufferTextureId(framebuffer);
}

} // namespace

namespace {
class ModelDlBuilder {
public:
    ModelDlBuilder(const ModelDrawParams& params, int width, int height, int targetFb)
        : mParams(params), mWidth(width), mHeight(height), mTargetFb(targetFb), mAspect((float)width / (float)height),
          mGfx(sGfx), mMtx(sMtx) {
    }

    Gfx* build(BKModelBin* const* models, int count, const ModelInstance* instances, int instCount,
               const SpriteBillboard* sprites, int spriteCount) {
        beginFrame();
        computeCamera();
        loadWorldProjection();
        setInitialRenderState();
        drawBackdrop();
        setModelViewBase();
        setEnvLookAt();
        primeModelRender(models, count);

        buildFrustum(mFrustum, mEye, mFocus, mParams.fovYDeg, mAspect, 1.0f, mFarPlane);
        drawSky();
        const int opaqueCount = drawOpaqueModels(models, count);
        const int pendingXlu = drawInstances(instances, instCount);
        drawSprites(sprites, spriteCount);
        drawTranslucent(models, count, opaqueCount, instances, instCount, pendingXlu);
        drawGizmos();
        drawSelectionBox();
        return endFrame();
    }

private:
    size_t gfxUsed() const {
        return (size_t)(mGfx - sGfx);
    }
    size_t mtxUsed() const {
        return (size_t)(mMtx - sMtx);
    }
    bool poolsExhausted() const {
        return gfxUsed() > kGfxSoftLimit || mtxUsed() > kMtxSoftLimit;
    }

    void beginFrame() {
        const int vpW = mParams.vpWidth > 0 ? mParams.vpWidth : mWidth;
        const int vpH = mParams.vpHeight > 0 ? mParams.vpHeight : mHeight;
        sVp.vp.vscale[0] = (short)(vpW * 2);
        sVp.vp.vscale[1] = (short)(vpH * 2);
        sVp.vp.vscale[2] = G_MAXZ;
        sVp.vp.vscale[3] = 0;
        sVp.vp.vtrans[0] = (short)(vpW * 2);
        sVp.vp.vtrans[1] = (short)(vpH * 2);
        sVp.vp.vtrans[2] = 0;
        sVp.vp.vtrans[3] = 0;

        mGfx->words.w0 = ((uintptr_t)0xDDu << 24) | (uint32_t)ucode_f3dex;
        mGfx->words.w1 = 0;
        ++mGfx;

        if (mTargetFb >= 0) {
            mGfx->words.w0 = (uintptr_t)G_SETFB << 24;
            mGfx->words.w1 = (uintptr_t)(uint32_t)mTargetFb;
            ++mGfx;
        }
        gSPViewport(mGfx++, &sVp);
        gDPSetScissor(mGfx++, G_SC_NON_INTERLACE, 0, 0, vpW, vpH);
    }

    void computeCamera() {
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        if (mParams.freeFly) {
            const float cosPitch = std::cos(rad(mParams.lookPitchDeg));
            const float fwd[3] = { cosPitch * std::sin(rad(mParams.lookYawDeg)), std::sin(rad(mParams.lookPitchDeg)),
                                   -cosPitch * std::cos(rad(mParams.lookYawDeg)) };
            for (int axis = 0; axis < 3; ++axis) {
                mEye[axis] = mParams.eye[axis];
                mFocus[axis] = mParams.eye[axis] + fwd[axis];
            }
        } else {
            const float cosPitch = std::cos(rad(mParams.pitchDeg));
            mEye[0] = mParams.center[0] + mParams.distance * cosPitch * std::sin(rad(mParams.yawDeg));
            mEye[1] = mParams.center[1] + mParams.distance * std::sin(rad(mParams.pitchDeg));
            mEye[2] = mParams.center[2] + mParams.distance * cosPitch * std::cos(rad(mParams.yawDeg));
            for (int axis = 0; axis < 3; ++axis) {
                mFocus[axis] = mParams.center[axis];
            }
        }

        mViewMtx = mMtx++;
        mViewRotMtx = mMtx++;
        setLookAtMtx(mViewMtx, mEye, mFocus, up);
        const float origin[3] = { 0.0f, 0.0f, 0.0f };
        const float fwd[3] = { mFocus[0] - mEye[0], mFocus[1] - mEye[1], mFocus[2] - mEye[2] };
        setLookAtMtx(mViewRotMtx, origin, fwd, up);
    }

    void loadProjectionWith(Mtx* proj, uint16_t perspNorm, Mtx* view) {
        gSPPerspNormalize(mGfx++, perspNorm);
        gSPMatrix(mGfx++, proj, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
        gSPMatrix(mGfx++, view, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    }
    void loadWorldProjectionMtx() {
        loadProjectionWith(mWorldProj, mWorldPerspNorm, mViewMtx);
    }

    void loadWorldProjection() {
        float nearp = mParams.nearOverride > 0.0f ? mParams.nearOverride : (mParams.distance - mParams.radius * 2.0f);
        if (nearp < 1.0f) {
            nearp = 1.0f;
        }
        mFarPlane =
            mParams.farOverride > 0.0f ? mParams.farOverride : (mParams.distance + mParams.radius * 2.0f + 100.0f);
        mWorldProj = mMtx++;
        setPerspectiveMtx(mWorldProj, &mWorldPerspNorm, mParams.fovYDeg, mAspect, nearp, mFarPlane);
        loadWorldProjectionMtx();
    }

    void setInitialRenderState() {
        gSPSegment(mGfx++, 0x00, (void*)0);
        gSPClearGeometryMode(mGfx++, G_ZBUFFER | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                                         G_TEXTURE_GEN_LINEAR | G_LOD | G_SHADING_SMOOTH);
        gSPTexture(mGfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gSPSetGeometryMode(mGfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
        gDPSetCycleType(mGfx++, G_CYC_1CYCLE);
        gDPPipelineMode(mGfx++, G_PM_NPRIMITIVE);
        gDPSetCombineMode(mGfx++, G_CC_SHADE, G_CC_SHADE);
        gDPSetAlphaCompare(mGfx++, G_AC_NONE);
        gDPSetColorDither(mGfx++, G_CD_DISABLE);
        gDPSetTextureFilter(mGfx++, G_TF_BILERP);
        gDPSetRenderMode(mGfx++, G_RM_AA_ZB_XLU_LINE, G_RM_AA_ZB_XLU_LINE2);
        gSPClipRatio(mGfx++, FRUSTRATIO_1);
        gDPPipeSync(mGfx++);
        gSPDisplayList(mGfx++, setup2CycleWhiteEnvDL);
        gSPSegment(mGfx++, 0x03, (void*)renderModesFullDepthOpa);
    }

    void drawBackdrop() {
        if (!mParams.drawBackdrop) {
            return;
        }
        const float zero3[3] = { 0.0f, 0.0f, 0.0f };
        Mtx* identity = mMtx++;
        setObjMtx(identity, zero3, zero3, 1.0f);
        auto corner = [](Vtx* vert, short x, short y) {
            vert->v.ob[0] = x;
            vert->v.ob[1] = y;
            vert->v.ob[2] = 0;
            vert->v.flag = 0;
            vert->v.tc[0] = 0;
            vert->v.tc[1] = 0;
            vert->v.cn[0] = 77;
            vert->v.cn[1] = 77;
            vert->v.cn[2] = 82;
            vert->v.cn[3] = 255;
        };
        corner(&sBackdropVtx[0], -2, 2);
        corner(&sBackdropVtx[1], 2, 2);
        corner(&sBackdropVtx[2], -2, -2);
        corner(&sBackdropVtx[3], 2, -2);

        gDPPipeSync(mGfx++);
        gSPMatrix(mGfx++, identity, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
        gSPMatrix(mGfx++, identity, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPClearGeometryMode(mGfx++,
                             G_ZBUFFER | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_BOTH | G_FOG);
        gSPSetGeometryMode(mGfx++, G_SHADE | G_SHADING_SMOOTH);
        gDPSetCycleType(mGfx++, G_CYC_1CYCLE);
        gSPTexture(mGfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetCombineMode(mGfx++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(mGfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
        gSPSegment(mGfx++, 0x04, (void*)sBackdropVtx);
        __gSPVertex(mGfx++, (void*)(uintptr_t)0x04000001, 4, 0);
        gSP2Triangles(mGfx++, 0, 1, 2, 0, 1, 3, 2, 0);
        gDPPipeSync(mGfx++);
        gSPSetGeometryMode(mGfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
        loadWorldProjectionMtx();
        gSPDisplayList(mGfx++, setup2CycleWhiteEnvDL);
        gSPSegment(mGfx++, 0x03, (void*)renderModesFullDepthOpa);
    }

    void setModelViewBase() {
        Mtx* base = mMtx++;
        const float zero3[3] = { 0.0f, 0.0f, 0.0f };
        setObjMtx(base, zero3, zero3, 1.0f);
        gSPMatrix(mGfx++, base, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    }

    void setEnvLookAt() {
        float look[3] = { mFocus[0] - mEye[0], mFocus[1] - mEye[1], mFocus[2] - mEye[2] };
        float lookLen = std::sqrt(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
        if (lookLen < 1e-6f) {
            look[0] = 0;
            look[1] = 0;
            look[2] = -1;
            lookLen = 1;
        }
        look[0] /= lookLen;
        look[1] /= lookLen;
        look[2] /= lookLen;

        float right[3] = { look[2], 0.0f, -look[0] };
        float rightLen = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        if (rightLen < 1e-6f) {
            right[0] = 1;
            right[2] = 0;
            rightLen = 1;
        }
        right[0] /= rightLen;
        right[2] /= rightLen;

        const float up[3] = { look[1] * right[2] - look[2] * right[1], look[2] * right[0] - look[0] * right[2],
                              look[0] * right[1] - look[1] * right[0] };
        auto toS8 = [](float value) {
            const int scaled = (int)(value * 127.0f);
            return (signed char)(scaled > 127 ? 127 : scaled < -128 ? -128 : scaled);
        };
        sEnvLookAt = LookAt{};
        for (int axis = 0; axis < 3; ++axis) {
            sEnvLookAt.l[0].l.dir[axis] = toS8(right[axis]);
            sEnvLookAt.l[1].l.dir[axis] = toS8(up[axis]);
        }
        gSPLookAt(mGfx++, &sEnvLookAt);
    }

    void primeModelRender(BKModelBin* const* models, int count) {
        static bool sInitialized = false;
        if (!sInitialized) {
            modelRender_init();
            modelRender_func_8033A25C(false);
            AnimTextureListCache_init();
            func_8034C97C();
            sInitialized = true;
        }
        if (!mParams.drawBackdrop) {
            for (int modelIdx = 0; modelIdx < count; ++modelIdx) {
                registerMapXforms(models[modelIdx]);
            }
        }
        static float sLastAnimTime = -1.0f;
        const float delta =
            (sLastAnimTime < 0.0f || mParams.animTime < sLastAnimTime) ? 0.0f : (mParams.animTime - sLastAnimTime);
        sLastAnimTime = mParams.animTime;
        lh_setFrameDelta(delta);
        AnimTextureListCache_update();
        func_8034C9D4();
    }

    void drawModel(BKModelBin* model, int depthMode, const float* pos, const float* rot, float scale,
                   const Mtx* bones = nullptr, int boneCount = 0) {
        if (!model) {
            return;
        }
        modelRender_reset();
        for (int selector = 1; selector < 0x2A; ++selector) {
            modelRender_func_8033A470(selector, 0x7FFFFFFF);
        }
        modelRender_setEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
        modelRender_setDepthMode(depthMode);
        if (bones && boneCount > 0) {
            lh_setEditorBones(reinterpret_cast<const float*>(bones), boneCount);
        }
        const int cacheId = animCacheFor(model);
        if (cacheId != 0) {
            modelRender_setAnimatedTexturesCacheId(cacheId);
        }
        float posBuf[3], rotBuf[3];
        float* posArg = nullptr;
        float* rotArg = nullptr;
        if (pos) {
            posBuf[0] = pos[0];
            posBuf[1] = pos[1];
            posBuf[2] = pos[2];
            posArg = posBuf;
        }
        if (rot) {
            rotBuf[0] = rot[0];
            rotBuf[1] = rot[1];
            rotBuf[2] = rot[2];
            rotArg = rotBuf;
        }
        modelRender_draw((void**)&mGfx, (void**)&mMtx, posArg, rotArg, scale, nullptr, (void*)model);
    }

    void drawSky() {
        if (mParams.skyCount <= 0 || !mParams.sky) {
            return;
        }
        Mtx* skyProj = mMtx++;
        uint16_t skyPerspNorm = 0xFFFF;
        setPerspectiveMtx(skyProj, &skyPerspNorm, mParams.fovYDeg, mAspect, 5.0f, 15000.0f);
        gDPPipeSync(mGfx++);
        loadProjectionWith(skyProj, skyPerspNorm, mViewRotMtx);
        gSPSegment(mGfx++, 0x03, (void*)renderModesNoDepthOpa);
        for (int layer = 0; layer < mParams.skyCount; ++layer) {
            const SkyLayer& sky = mParams.sky[layer];
            if (!sky.model || sky.model->vtx_list_offset == 0) {
                continue;
            }
            const float rot[3] = { 0.0f, sky.rotYDeg, 0.0f };
            drawModel(sky.model, MODEL_RENDER_DEPTH_NONE, nullptr, rot, sky.scale);
        }
        gDPPipeSync(mGfx++);
        loadWorldProjectionMtx();
        gSPSegment(mGfx++, 0x03, (void*)renderModesFullDepthOpa);
    }

    int drawOpaqueModels(BKModelBin* const* models, int count) {
        const int opaqueCount = (mParams.opaqueModelCount < 0)
                                    ? count
                                    : (mParams.opaqueModelCount > count ? count : mParams.opaqueModelCount);
        for (int modelIdx = 0; modelIdx < opaqueCount; ++modelIdx) {
            drawModel(models[modelIdx], MODEL_RENDER_DEPTH_FULL, nullptr, nullptr, 1.0f, mParams.boneMtx,
                      mParams.boneCount);
        }
        return opaqueCount;
    }

    void measureInstance(int instIdx, const Gfx* from) {
        BoundsWalk walk;
        walk.run(from, mGfx, 0);
        walk.store(sInstanceBounds[instIdx]);
    }

    int drawInstances(const ModelInstance* instances, int instCount) {
        if (instCount <= 0) {
            return 0;
        }
        sInstanceBounds.assign(instCount, DrawnBounds{});
        int pendingXlu = 0;
        for (int instIdx = 0; instIdx < instCount; ++instIdx) {
            const ModelInstance& inst = instances[instIdx];
            if (!inst.model || instanceCulled(mFrustum, inst.model, inst.pos, inst.scale)) {
                continue;
            }
            if (poolsExhausted()) {
                break;
            }
            if (modelIsPureXlu(inst.model)) {
                ++pendingXlu;
                continue;
            }
            const Gfx* from = mGfx;
            drawModel(inst.model, MODEL_RENDER_DEPTH_FULL, inst.pos, inst.rotDeg, inst.scale);
            measureInstance(instIdx, from);
        }
        return pendingXlu;
    }

    Vtx* allocGizmoVerts(int count) {
        if (mVtxUsed + count > (int)(sizeof(sVtx) / sizeof(sVtx[0]))) {
            return nullptr;
        }
        Vtx* verts = &sVtx[mVtxUsed];
        mVtxUsed += count;
        return verts;
    }

    static void setGizmoVert(Vtx& vert, const float p[3], const uint8_t color[3], uint8_t alpha = 255) {
        vert.v.ob[0] = p[0];
        vert.v.ob[1] = p[1];
        vert.v.ob[2] = p[2];
        vert.v.flag = 0;
        vert.v.tc[0] = 0;
        vert.v.tc[1] = 0;
        vert.v.cn[0] = color[0];
        vert.v.cn[1] = color[1];
        vert.v.cn[2] = color[2];
        vert.v.cn[3] = alpha;
    }

    void emitGizmoBatch(Vtx* verts, int count) {
        gSPSegment(mGfx++, 0x04, (void*)verts);
        __gSPVertex(mGfx++, (void*)(uintptr_t)0x04000001, count, 0);
    }

    // This pipeline has no line primitive, so every edge becomes a thin quad
    // turned to face the camera.
    void drawWireEdges(const float (*edges)[2][3], int edgeCount, const uint8_t color[3], float thickness,
                       const float viewDir[3]) {
        for (int first = 0; first < edgeCount; first += 8) {
            const int batch = edgeCount - first > 8 ? 8 : edgeCount - first;
            Vtx* verts = allocGizmoVerts(batch * 4);
            if (!verts) {
                return;
            }
            for (int edge = 0; edge < batch; ++edge) {
                const float* from = edges[first + edge][0];
                const float* to = edges[first + edge][1];
                float along[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
                normalize3(along);
                float perp[3];
                cross3(perp, along, viewDir);
                normalize3(perp);
                for (int step = 0; step < 4; ++step) {
                    const float* base = (step < 2) ? from : to;
                    const float side = (step == 0 || step == 3) ? 0.5f : -0.5f;
                    float p[3];
                    for (int axis = 0; axis < 3; ++axis) {
                        p[axis] = base[axis] + perp[axis] * thickness * side;
                    }
                    setGizmoVert(verts[edge * 4 + step], p, color);
                }
            }
            emitGizmoBatch(verts, batch * 4);
            for (int edge = 0; edge < batch; ++edge) {
                const int base = edge * 4;
                gSP2Triangles(mGfx++, base, base + 1, base + 2, 0, base, base + 2, base + 3, 0);
            }
        }
    }

    void drawCubeGizmo(const GizmoInstance& gizmo) {
        Vtx* verts = allocGizmoVerts(8);
        if (!verts) {
            return;
        }
        for (int corner = 0; corner < 8; ++corner) {
            const float p[3] = { gizmo.pos[0] + ((corner & 1) ? kCubeHalf : -kCubeHalf),
                                 gizmo.pos[1] + ((corner & 2) ? kCubeHalf : -kCubeHalf),
                                 gizmo.pos[2] + ((corner & 4) ? kCubeHalf : -kCubeHalf) };
            setGizmoVert(verts[corner], p, gizmo.color);
        }
        emitGizmoBatch(verts, 8);
        static const uint8_t kFaces[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
                                              { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
        for (const auto& f : kFaces) {
            gSP2Triangles(mGfx++, f[0], f[1], f[2], 0, f[0], f[2], f[3], 0);
        }
    }

    void drawPyramidGizmo(const GizmoInstance& gizmo) {
        Vtx* verts = allocGizmoVerts(5);
        if (!verts) {
            return;
        }
        const float apex[3] = { gizmo.pos[0], gizmo.pos[1] + kPyramidApexY, gizmo.pos[2] };
        setGizmoVert(verts[0], apex, gizmo.color);
        static const float kBase[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
        for (int corner = 0; corner < 4; ++corner) {
            const float p[3] = { gizmo.pos[0] + kBase[corner][0] * kCubeHalf, gizmo.pos[1] - kCubeHalf,
                                 gizmo.pos[2] + kBase[corner][1] * kCubeHalf };
            setGizmoVert(verts[1 + corner], p, gizmo.color);
        }
        emitGizmoBatch(verts, 5);
        gSP2Triangles(mGfx++, 0, 1, 2, 0, 0, 2, 3, 0);
        gSP2Triangles(mGfx++, 0, 3, 4, 0, 0, 4, 1, 0);
        gSP2Triangles(mGfx++, 1, 4, 3, 0, 1, 3, 2, 0);
    }

    void drawCameraGizmo(const GizmoInstance& gizmo, const float viewDir[3]) {
        float rot[3][3];
        gizmoRotationMtx(gizmo.pitchYawRollDeg, rot);
        float world[8][3];
        for (int point = 0; point < 8; ++point) {
            for (int axis = 0; axis < 3; ++axis) {
                world[point][axis] = gizmo.pos[axis] + rot[axis][0] * kCameraPoints[point][0] +
                                     rot[axis][1] * kCameraPoints[point][1] + rot[axis][2] * kCameraPoints[point][2];
            }
        }

        // Pale translucent body so it reads at distance; the wire keeps the exact palette color.
        const uint8_t fill[3] = { (uint8_t)((gizmo.color[0] + 510) / 3), (uint8_t)((gizmo.color[1] + 510) / 3),
                                  (uint8_t)((gizmo.color[2] + 510) / 3) };
        if (Vtx* body = allocGizmoVerts(5)) {
            for (int point = 0; point < 5; ++point) {
                setGizmoVert(body[point], world[point], fill, 110);
            }
            gDPPipeSync(mGfx++);
            gDPSetRenderMode(mGfx++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
            emitGizmoBatch(body, 5);
            gSP2Triangles(mGfx++, 0, 1, 3, 0, 0, 3, 2, 0);
            gSP2Triangles(mGfx++, 0, 4, 1, 0, 1, 4, 3, 0);
            gSP2Triangles(mGfx++, 3, 4, 2, 0, 2, 4, 0, 0);
            gDPPipeSync(mGfx++);
            gDPSetRenderMode(mGfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        }

        static const uint8_t kEdges[8][2] = { { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 },
                                              { 0, 4 }, { 1, 4 }, { 2, 4 }, { 3, 4 } };
        float edges[8][2][3];
        for (int edge = 0; edge < 8; ++edge) {
            for (int axis = 0; axis < 3; ++axis) {
                edges[edge][0][axis] = world[kEdges[edge][0]][axis];
                edges[edge][1][axis] = world[kEdges[edge][1]][axis];
            }
        }
        drawWireEdges(edges, 8, gizmo.color, 2.0f, viewDir);

        Vtx* verts = allocGizmoVerts(3);
        if (!verts) {
            return;
        }
        static const uint8_t kBlack[3] = { 0, 0, 0 };
        setGizmoVert(verts[0], world[5], kBlack);
        setGizmoVert(verts[1], world[7], kBlack);
        setGizmoVert(verts[2], world[6], kBlack);
        emitGizmoBatch(verts, 3);
        gSP2Triangles(mGfx++, 0, 1, 2, 0, 2, 1, 0, 0);
    }

    void drawSphereGizmo(const GizmoInstance& gizmo, const float viewDir[3]) {
        constexpr int kSlices = 5, kStacks = 5;
        float pts[kStacks + 1][kSlices][3];
        for (int stack = 0; stack <= kStacks; ++stack) {
            const float phi = rad(-90.0f + 180.0f * stack / kStacks);
            for (int slice = 0; slice < kSlices; ++slice) {
                const float theta = rad(360.0f * slice / kSlices);
                pts[stack][slice][0] = gizmo.pos[0] + gizmo.radius * std::cos(phi) * std::cos(theta);
                pts[stack][slice][1] = gizmo.pos[1] + gizmo.radius * std::sin(phi);
                pts[stack][slice][2] = gizmo.pos[2] + gizmo.radius * std::cos(phi) * std::sin(theta);
            }
        }
        float edges[kSlices * kStacks + kSlices * (kStacks - 1)][2][3];
        int edgeCount = 0;
        auto addEdge = [&](const float* a, const float* b) {
            for (int axis = 0; axis < 3; ++axis) {
                edges[edgeCount][0][axis] = a[axis];
                edges[edgeCount][1][axis] = b[axis];
            }
            ++edgeCount;
        };
        for (int slice = 0; slice < kSlices; ++slice) {
            for (int stack = 0; stack < kStacks; ++stack) {
                addEdge(pts[stack][slice], pts[stack + 1][slice]);
            }
        }
        for (int stack = 1; stack < kStacks; ++stack) {
            for (int slice = 0; slice < kSlices; ++slice) {
                addEdge(pts[stack][slice], pts[stack][(slice + 1) % kSlices]);
            }
        }
        float thickness = gizmo.radius * 0.012f;
        if (thickness < 2.0f) {
            thickness = 2.0f;
        }
        drawWireEdges(edges, edgeCount, gizmo.color, thickness, viewDir);
    }

    void drawWireBoxGizmo(const GizmoInstance& gizmo, const float viewDir[3]) {
        float lo[3], hi[3], span = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = gizmo.pos[axis] - gizmo.halfExtent[axis];
            hi[axis] = gizmo.pos[axis] + gizmo.halfExtent[axis];
            if (gizmo.halfExtent[axis] * 2.0f > span) {
                span = gizmo.halfExtent[axis] * 2.0f;
            }
        }
        float edges[12][2][3];
        boxEdges(lo, hi, edges);
        float thickness = span * 0.003f;
        if (thickness < 4.0f) {
            thickness = 4.0f;
        }
        drawWireEdges(edges, 12, gizmo.color, thickness, viewDir);
    }

    void drawGizmos() {
        if (!mParams.gizmos || mParams.gizmoCount <= 0) {
            return;
        }
        float viewDir[3] = { mFocus[0] - mEye[0], mFocus[1] - mEye[1], mFocus[2] - mEye[2] };
        normalize3(viewDir);

        gDPPipeSync(mGfx++);
        Mtx* identity = mMtx++;
        const float zero3[3] = { 0.0f, 0.0f, 0.0f };
        setObjMtx(identity, zero3, zero3, 1.0f);
        gSPMatrix(mGfx++, identity, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPClearGeometryMode(mGfx++, G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_BOTH | G_FOG);
        gSPSetGeometryMode(mGfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
        gDPSetCycleType(mGfx++, G_CYC_1CYCLE);
        gSPTexture(mGfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetCombineMode(mGfx++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(mGfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);

        for (int index = 0; index < mParams.gizmoCount; ++index) {
            if (poolsExhausted()) {
                break;
            }
            const GizmoInstance& gizmo = mParams.gizmos[index];
            switch (gizmo.kind) {
                case GIZMO_CUBE:
                    drawCubeGizmo(gizmo);
                    break;
                case GIZMO_PYRAMID:
                    drawPyramidGizmo(gizmo);
                    break;
                case GIZMO_CAMERA:
                    drawCameraGizmo(gizmo, viewDir);
                    break;
                case GIZMO_SPHERE:
                    drawSphereGizmo(gizmo, viewDir);
                    break;
                case GIZMO_WIREBOX:
                    drawWireBoxGizmo(gizmo, viewDir);
                    break;
                default:
                    break;
            }
        }
        gDPPipeSync(mGfx++);
    }

    void drawSelectionBox() {
        if (!mParams.selectionValid) {
            return;
        }
        const float* lo = mParams.selectionMin;
        const float* hi = mParams.selectionMax;
        float viewDir[3] = { mFocus[0] - mEye[0], mFocus[1] - mEye[1], mFocus[2] - mEye[2] };
        normalize3(viewDir);

        float span = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float extent = hi[axis] - lo[axis];
            if (extent > span) {
                span = extent;
            }
        }
        float thickness = span * 0.012f;
        if (thickness < 2.0f) {
            thickness = 2.0f;
        }
        float edges[12][2][3];
        boxEdges(lo, hi, edges);

        gDPPipeSync(mGfx++);

        Mtx* identity = mMtx++;
        const float zero3[3] = { 0.0f, 0.0f, 0.0f };
        setObjMtx(identity, zero3, zero3, 1.0f);
        gSPMatrix(mGfx++, identity, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPClearGeometryMode(mGfx++, G_ZBUFFER | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_BOTH);
        gSPSetGeometryMode(mGfx++, G_SHADE | G_SHADING_SMOOTH);
        gDPSetCycleType(mGfx++, G_CYC_1CYCLE);
        gSPTexture(mGfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetCombineMode(mGfx++, G_CC_SHADE, G_CC_SHADE);
        gDPSetRenderMode(mGfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);

        static const uint8_t kSelectionColor[3] = { 255, 216, 51 };
        drawWireEdges(edges, 12, kSelectionColor, thickness, viewDir);
        gDPPipeSync(mGfx++);
    }

    void drawSprites(const SpriteBillboard* sprites, int spriteCount) {
        if (spriteCount <= 0) {
            return;
        }
        float right[3];
        if (mParams.freeFly) {
            right[0] = std::cos(rad(mParams.lookYawDeg));
            right[2] = std::sin(rad(mParams.lookYawDeg));
        } else {
            right[0] = std::cos(rad(mParams.yawDeg));
            right[2] = -std::sin(rad(mParams.yawDeg));
        }
        right[1] = 0.0f;
        const float up[3] = { 0.0f, 1.0f, 0.0f };

        for (int spriteIdx = 0; spriteIdx < spriteCount; ++spriteIdx) {
            const SpriteBillboard& billboard = sprites[spriteIdx];
            if (!billboard.texels) {
                continue;
            }
            const float extentX = billboard.x1 - billboard.x0;
            const float extentY = billboard.y1 - billboard.y0;
            const float radius = ((extentX > extentY) ? extentX : extentY) + 1.0f;
            if (mFrustum.cullSphere(billboard.pos, radius)) {
                continue;
            }
            if (gfxUsed() > kGfxSoftLimit || mVtxUsed + 4 > (int)(sizeof(sVtx) / sizeof(sVtx[0]))) {
                break;
            }
            Vtx* verts = &sVtx[mVtxUsed];
            mVtxUsed += 4;
            drawBillboard(mGfx, verts, billboard, right, up);
        }
    }

    void drawTranslucent(BKModelBin* const* models, int count, int opaqueCount, const ModelInstance* instances,
                         int instCount, int pendingXlu) {
        if (opaqueCount >= count && pendingXlu <= 0) {
            return;
        }
        gDPPipeSync(mGfx++);
        gSPSegment(mGfx++, 0x03, (void*)renderModesDepthCompareXlu);
        for (int modelIdx = opaqueCount; modelIdx < count; ++modelIdx) {
            drawModel(models[modelIdx], MODEL_RENDER_DEPTH_FULL, nullptr, nullptr, 1.0f);
        }
        for (int instIdx = 0; instIdx < instCount && pendingXlu > 0; ++instIdx) {
            const ModelInstance& inst = instances[instIdx];
            if (inst.model && instanceCulled(mFrustum, inst.model, inst.pos, inst.scale)) {
                continue;
            }
            if (!inst.model || !modelIsPureXlu(inst.model)) {
                continue;
            }
            if (poolsExhausted()) {
                break;
            }
            const Gfx* from = mGfx;
            drawModel(inst.model, MODEL_RENDER_DEPTH_COMPARE, inst.pos, inst.rotDeg, inst.scale);
            measureInstance(instIdx, from);
        }
    }

    Gfx* endFrame() {
        if (mTargetFb >= 0) {
            mGfx->words.w0 = (uintptr_t)G_RESETFB << 24;
            mGfx->words.w1 = 0;
            ++mGfx;
        }
        gDPFullSync(mGfx++);
        gSPEndDisplayList(mGfx++);
        return sGfx;
    }

    const ModelDrawParams& mParams;
    int mWidth = 0;
    int mHeight = 0;
    int mTargetFb = -1;
    float mAspect = 1.0f;
    float mFarPlane = 1.0f;

    Gfx* mGfx = nullptr;
    Mtx* mMtx = nullptr;
    int mVtxUsed = 0;

    float mEye[3] = { 0.0f, 0.0f, 0.0f };
    float mFocus[3] = { 0.0f, 0.0f, 0.0f };
    Mtx* mViewMtx = nullptr;
    Mtx* mViewRotMtx = nullptr;
    Mtx* mWorldProj = nullptr;
    uint16_t mWorldPerspNorm = 0xFFFF;
    Frustum mFrustum;
};

Gfx* BuildModelsDL(BKModelBin* const* models, int count, int width, int height, const ModelDrawParams& p, int targetFb,
                   const ModelInstance* instances = nullptr, int instCount = 0,
                   const SpriteBillboard* sprites = nullptr, int spriteCount = 0) {
    if (count < 0 || (count > 0 && !models) || width <= 0 || height <= 0) {
        return nullptr;
    }
    ModelDlBuilder builder(p, width, height, targetFb);
    return builder.build(models, count, instances, instCount, sprites, spriteCount);
}

} // namespace

void GizmoBounds(const GizmoInstance& gizmo, float outMin[3], float outMax[3]) {
    switch (gizmo.kind) {
        case GIZMO_PYRAMID:
            for (int axis = 0; axis < 3; ++axis) {
                outMin[axis] = gizmo.pos[axis] - kCubeHalf;
                outMax[axis] = gizmo.pos[axis] + kCubeHalf;
            }
            outMax[1] = gizmo.pos[1] + kPyramidApexY;
            return;
        case GIZMO_CAMERA: {
            float rot[3][3];
            gizmoRotationMtx(gizmo.pitchYawRollDeg, rot);
            for (int axis = 0; axis < 3; ++axis) {
                outMin[axis] = outMax[axis] = gizmo.pos[axis];
            }
            for (const float* local : kCameraPoints) {
                for (int axis = 0; axis < 3; ++axis) {
                    const float value =
                        gizmo.pos[axis] + rot[axis][0] * local[0] + rot[axis][1] * local[1] + rot[axis][2] * local[2];
                    if (value < outMin[axis]) {
                        outMin[axis] = value;
                    }
                    if (value > outMax[axis]) {
                        outMax[axis] = value;
                    }
                }
            }
            return;
        }
        case GIZMO_SPHERE:
            for (int axis = 0; axis < 3; ++axis) {
                outMin[axis] = gizmo.pos[axis] - gizmo.radius;
                outMax[axis] = gizmo.pos[axis] + gizmo.radius;
            }
            return;
        case GIZMO_WIREBOX:
            for (int axis = 0; axis < 3; ++axis) {
                outMin[axis] = gizmo.pos[axis] - gizmo.halfExtent[axis];
                outMax[axis] = gizmo.pos[axis] + gizmo.halfExtent[axis];
            }
            return;
        default:
            for (int axis = 0; axis < 3; ++axis) {
                outMin[axis] = gizmo.pos[axis] - kCubeHalf;
                outMax[axis] = gizmo.pos[axis] + kCubeHalf;
            }
            return;
    }
}

void ResetMapXforms() {
    registeredMapXforms().clear();
    func_8034C8D8();
    func_8034C97C();
}

bool DrawnInstanceBounds(int instIdx, float outMin[3], float outMax[3]) {
    if (instIdx < 0 || instIdx >= (int)sInstanceBounds.size() || !sInstanceBounds[instIdx].valid) {
        return false;
    }
    const DrawnBounds& bounds = sInstanceBounds[instIdx];
    for (int axis = 0; axis < 3; ++axis) {
        outMin[axis] = bounds.min[axis];
        outMax[axis] = bounds.max[axis];
    }
    return true;
}

} // namespace Lightbulb

namespace Lightbulb {
void* RenderModelsPreview(BKModelBin* const* models, int count, int width, int height, const ModelDrawParams& params,
                          int viewId) {
    if (!models || count <= 0 || !models[0] || width <= 0 || height <= 0) {
        return nullptr;
    }
    auto interpreter = fastInterpreter();
    if (!interpreter) {
        return nullptr;
    }
    const int framebuffer = previewFramebuffer(*interpreter, 0, viewId, width, height);
    ModelDrawParams previewParams = params;
    previewParams.drawBackdrop = true;
    Gfx* displayList = BuildModelsDL(models, count, width, height, previewParams, framebuffer);
    return displayList ? runToFramebuffer(*interpreter, displayList, framebuffer) : nullptr;
}

void* RenderModelPreview(BKModelBin* model, int width, int height, const ModelDrawParams& params, int viewId) {
    return RenderModelsPreview(&model, 1, width, height, params, viewId);
}

void AppendSpriteBillboards(const O2rSpriteTex& sprite, int frameIndex, int onlyChunk, const float pos[3], float scale,
                            bool mirror, bool pixelAspect, std::vector<SpriteBillboard>& out) {
    if (!sprite.loaded || frameIndex < 0 || frameIndex >= (int)sprite.frames.size()) {
        return;
    }
    const O2rSpriteFrame& frame = sprite.frames[frameIndex];
    const float mul = pixelAspect ? 1.0f : scale;
    for (size_t chunkIdx = 0; chunkIdx < frame.chunks.size(); ++chunkIdx) {
        const O2rSpriteChunk& chunk = frame.chunks[chunkIdx];
        if (!chunk.texels || (onlyChunk >= 0 && (int)chunkIdx != onlyChunk)) {
            continue;
        }
        SpriteBillboard billboard;
        billboard.texels = chunk.texels;
        billboard.tlut = chunk.tlut;
        billboard.width = chunk.width;
        billboard.height = chunk.height;
        billboard.fmt = chunk.fmt;
        billboard.siz = chunk.siz;
        billboard.tlutColors = chunk.tlutColors;
        billboard.pos[0] = pos[0];
        billboard.pos[1] = pos[1];
        billboard.pos[2] = pos[2];
        SpriteChunkRect(sprite, frame, chunk, pixelAspect, billboard.x0, billboard.x1, billboard.y0, billboard.y1);
        billboard.x0 *= mul;
        billboard.x1 *= mul;
        billboard.y0 *= mul;
        billboard.y1 *= mul;
        billboard.mirror = mirror;
        out.push_back(billboard);
    }
}

void* RenderSpritePreview(const O2rSpriteTex& sprite, int frameIndex, bool mirror, int width, int height, float yawDeg,
                          float pitchDeg, int viewId, int chunkIndex) {
    if (!sprite.loaded || frameIndex < 0 || frameIndex >= (int)sprite.frames.size() || width <= 0 || height <= 0) {
        return nullptr;
    }
    auto interpreter = fastInterpreter();
    if (!interpreter) {
        return nullptr;
    }

    const bool lone = chunkIndex >= 0;
    const float origin[3] = { 0.0f, 0.0f, 0.0f };
    std::vector<SpriteBillboard> sprs;
    AppendSpriteBillboards(sprite, frameIndex, chunkIndex, origin, 1.0f, mirror, lone, sprs);
    if (sprs.empty()) {
        return nullptr;
    }

    float minx = 0.0f, maxx = 0.0f, miny = 0.0f, maxy = 0.0f;
    bool first = true;
    for (size_t frameIdx = 0; frameIdx < sprite.frames.size(); ++frameIdx) {
        if (lone && (int)frameIdx != frameIndex) {
            continue;
        }
        const Lightbulb::O2rSpriteFrame& f = sprite.frames[frameIdx];
        for (size_t chunkIdx = 0; chunkIdx < f.chunks.size(); ++chunkIdx) {
            if (lone && (int)chunkIdx != chunkIndex) {
                continue;
            }
            float x0, x1, y0, y1;
            SpriteChunkRect(sprite, f, f.chunks[chunkIdx], lone, x0, x1, y0, y1);
            if (first) {
                minx = x0, maxx = x1, miny = y0, maxy = y1, first = false;
            }
            if (x0 < minx)
                minx = x0;
            if (x1 > maxx)
                maxx = x1;
            if (y0 < miny)
                miny = y0;
            if (y1 > maxy)
                maxy = y1;
        }
    }
    const float cx = (minx + maxx) * 0.5f;
    const float cy = (miny + maxy) * 0.5f;
    for (SpriteBillboard& billboard : sprs) {
        billboard.x0 -= cx;
        billboard.x1 -= cx;
        billboard.y0 -= cy;
        billboard.y1 -= cy;
    }
    float ext = (maxx - minx) * 0.5f;
    const float exty = (maxy - miny) * 0.5f;
    if (exty > ext) {
        ext = exty;
    }
    if (ext < 1.0f) {
        ext = 1.0f;
    }

    const int sFb = previewFramebuffer(*interpreter, 1, viewId, width, height);

    ModelDrawParams drawParams;
    drawParams.yawDeg = yawDeg;
    drawParams.pitchDeg = pitchDeg;
    drawParams.center[0] = drawParams.center[1] = drawParams.center[2] = 0.0f;
    drawParams.radius = ext;
    drawParams.fovYDeg = 40.0f;
    drawParams.drawBackdrop = true;
    const float tanHalf = std::tan(rad(drawParams.fovYDeg) * 0.5f);
    drawParams.distance = ext * 1.15f / tanHalf;

    Gfx* displayList =
        BuildModelsDL(nullptr, 0, width, height, drawParams, sFb, nullptr, 0, sprs.data(), (int)sprs.size());
    if (!displayList) {
        return nullptr;
    }
    return runToFramebuffer(*interpreter, displayList, sFb);
}

bool RenderModelsAsGameFrame(BKModelBin* const* models, int count, const ModelDrawParams& params,
                             const ModelInstance* instances, int instCount, const SpriteBillboard* sprites,
                             int spriteCount) {
    if (!models || count <= 0 || !models[0]) {
        return false;
    }
    auto interpreter = fastInterpreter();
    if (!interpreter) {
        return false;
    }
    const int width = (int)interpreter->mCurDimensions.width;
    const int height = (int)interpreter->mCurDimensions.height;
    if (width < 16 || height < 16) {
        return false;
    }
    ModelDrawParams frameParams = params;
    frameParams.vpWidth = (int)interpreter->mNativeDimensions.width;
    frameParams.vpHeight = (int)interpreter->mNativeDimensions.height;
    Gfx* displayList =
        BuildModelsDL(models, count, width, height, frameParams, -1, instances, instCount, sprites, spriteCount);
    if (!displayList) {
        return false;
    }
    std::unordered_map<Mtx*, MtxF> noReplacements;
    interpreter->Run(displayList, noReplacements);
    return true;
}

bool GetGameViewportRect(float& x, float& y, float& w, float& h) {
    auto interpreter = fastInterpreter();
    if (!interpreter) {
        return false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    x = vp->WorkPos.x + (float)interpreter->mGameWindowViewport.x;
    y = vp->WorkPos.y + (float)interpreter->mGameWindowViewport.y;
    w = (float)interpreter->mGameWindowViewport.width;
    h = (float)interpreter->mGameWindowViewport.height;
    return w > 0.0f && h > 0.0f;
}

} // namespace Lightbulb
