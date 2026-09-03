#pragma once

#include <cstdint>
#include <libultraship/libultra/gbi.h>
#include <vector>

#ifndef gSPDisplayList
#define gSPDisplayList(pkt, dl) __gSPDisplayList(pkt, dl)
#endif
#ifndef gSPSegment
#define gSPSegment(pkt, seg, base) __gSPSegment(pkt, seg, base)
#endif

extern "C" {
extern Gfx setup2CycleDL[];
extern Gfx setup2CycleBlackPrimDL[];
extern Gfx setup2CycleWhiteEnvDL[];
extern Gfx setup2CycleDL_copy[];
extern Gfx mipMapClampDL[];
extern Gfx mipMapWrapDL[];
extern Gfx renderModesNoDepthOpa[][2];
extern Gfx renderModesFullDepthOpa[][2];
extern Gfx renderModesDepthCompareOpa[][2];
extern Gfx renderModesNoDepthXlu[][2];
extern Gfx renderModesFullDepthXlu[][2];
extern Gfx renderModesDepthCompareXlu[][2];
}

typedef struct bk_model_header_s BKModelBin;

namespace Lightbulb {
struct SkyLayer {
    BKModelBin* model = nullptr;
    float rotYDeg = 0.0f;
    float scale = 1.0f;
};

inline constexpr uint8_t kRareCameraColors[8][3] = {
    { 0xFF, 0x00, 0x00 }, // Red
    { 0x00, 0xFF, 0x00 }, // Green
    { 0x00, 0x00, 0xFF }, // Blue
    { 0xFF, 0xFF, 0x00 }, // Yellow
    { 0xFF, 0x00, 0xFF }, // Pink
    { 0xFF, 0xFF, 0xFF }, // White
    { 0xFF, 0x80, 0x00 }, // Orange
    { 0x00, 0xFF, 0xFF }, // Cyan
};

enum GizmoKind { GIZMO_CUBE = 0, GIZMO_PYRAMID = 1, GIZMO_CAMERA = 2, GIZMO_SPHERE = 3, GIZMO_WIREBOX = 4 };
struct GizmoInstance {
    int kind = GIZMO_CUBE;
    float pos[3] = { 0, 0, 0 };
    float pitchYawRollDeg[3] = { 0, 0, 0 };
    float radius = 0.0f;
    float halfExtent[3] = { 0, 0, 0 };
    uint8_t color[3] = { 255, 255, 255 };
};
void GizmoBounds(const GizmoInstance& gizmo, float outMin[3], float outMax[3]);
struct ModelDrawParams {
    float yawDeg = 30.0f;
    float pitchDeg = 20.0f;
    float distance = 1000.0f;
    float center[3] = { 0, 0, 0 };
    float fovYDeg = 40.0f;
    float radius = 1000.0f;
    bool freeFly = false;
    float eye[3] = { 0, 0, 0 };
    float lookYawDeg = 0.0f;
    float lookPitchDeg = 0.0f;
    float nearOverride = 0.0f;
    float farOverride = 0.0f;
    int vpWidth = 0;
    int vpHeight = 0;
    bool drawBackdrop = false;
    float animTime = 0.0f;
    const Mtx* boneMtx = nullptr;
    int boneCount = 0;
    int opaqueModelCount = -1;
    const SkyLayer* sky = nullptr;
    int skyCount = 0;

    bool selectionValid = false;
    float selectionMin[3] = { 0, 0, 0 };
    float selectionMax[3] = { 0, 0, 0 };
    const GizmoInstance* gizmos = nullptr;
    int gizmoCount = 0;
};
struct ModelInstance {
    BKModelBin* model = nullptr;
    float pos[3] = { 0, 0, 0 };
    float rotDeg[3] = { 0, 0, 0 };
    float scale = 1.0f;
};
struct SpriteBillboard {
    const uint8_t* texels = nullptr;
    const uint8_t* tlut = nullptr;
    int width = 0, height = 0, fmt = 0, siz = 0, tlutColors = 0;
    float pos[3] = { 0, 0, 0 };
    float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    bool mirror = false;
};
bool DrawnInstanceBounds(int instIdx, float outMin[3], float outMax[3]);
void ResetMapXforms();
} // namespace Lightbulb

namespace Lightbulb {
struct O2rSpriteTex;
void AppendSpriteBillboards(const O2rSpriteTex& sprite, int frameIndex, int onlyChunk, const float pos[3], float scale,
                            bool mirror, bool pixelAspect, std::vector<SpriteBillboard>& out);

void* RenderSpritePreview(const O2rSpriteTex& sprite, int frame, bool mirror, int width, int height, float yawDeg,
                          float pitchDeg, int viewId = 0, int chunk = -1);
void* RenderModelPreview(BKModelBin* model, int width, int height, const ModelDrawParams& params, int viewId = 0);
void* RenderModelsPreview(BKModelBin* const* models, int count, int width, int height, const ModelDrawParams& params,
                          int viewId = 0);
bool RenderModelsAsGameFrame(BKModelBin* const* models, int count, const ModelDrawParams& params,
                             const ModelInstance* instances = nullptr, int instCount = 0,
                             const SpriteBillboard* sprites = nullptr, int spriteCount = 0);
bool GetGameViewportRect(float& x, float& y, float& w, float& h);
} // namespace Lightbulb
