#include "App.h"
#include "LevelView.h"
#include "O2rImport.h"
#include "PreviewScene.h"
extern "C" {
#include "model.h"
}

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {
using Lightbulb::kDeg;
using Lightbulb::kLevelFovYDeg;

void setRareCameraColor(uint8_t out[3], int cameraIndex) {
    const uint8_t* color = Lightbulb::kRareCameraColors[cameraIndex & 7];
    out[0] = color[0];
    out[1] = color[1];
    out[2] = color[2];
}
} // namespace

bool App::RenderLevelGameFrame() {
    LevelScene& scene = mLevelScene;
    if (!mO2rLoaded || scene.sel < 0 || scene.sel >= (int)scene.entries.size()) {
        return false;
    }
    std::vector<BKModelBin*> models;
    int opaqueChunks = 0;
    const auto& chunkPaths = scene.entries[scene.sel].chunks;
    for (size_t chunkIndex = 0; chunkIndex < chunkPaths.size(); ++chunkIndex) {
        if (BKModelBin* cm = Lightbulb::LoadO2rModel(chunkPaths[chunkIndex])) {
            models.push_back(cm);
            if (chunkIndex == 0) {
                opaqueChunks = 1;
            }
        }
    }
    if (models.empty()) {
        return false;
    }
    BKModelBin* firstChunk = models[0];
    if (firstChunk->vtx_list_offset != 0 && !scene.framed) {
        const BKVertexList* vtxList = modelbin_getVtxList(firstChunk);
        const float radius = (float)(vtxList->global_norm > 0 ? vtxList->global_norm : 1000);

        // Load in at the lowest numbered entry point.
        const Lightbulb::SetupNode* entry = nullptr;
        for (const Lightbulb::SetupNode& nd : mSetup.nodes) {
            if (!nd.script && nd.category == 6 && Lightbulb::EditorEntryPointId(nd.id) &&
                (!entry || nd.id < entry->id)) {
                entry = &nd;
            }
        }
        const Lightbulb::SetupCamera* camera = nullptr;
        for (const Lightbulb::SetupCamera& sc : mSetup.cameras) {
            if (sc.type == 2) {
                camera = &sc;
                break;
            }
        }
        if (!camera && !mSetup.cameras.empty()) {
            camera = &mSetup.cameras.front();
        }
        if (entry) {
            FrameEyeAtEntry(*entry);
        } else if (camera) {
            scene.eye[0] = camera->pos[0];
            scene.eye[1] = camera->pos[1];
            scene.eye[2] = camera->pos[2];
            scene.pitch = camera->pitchYawRoll[0];
            scene.yaw = camera->pitchYawRoll[1];
            while (scene.pitch > 180.0f)
                scene.pitch -= 360.0f;
            while (scene.pitch < -180.0f)
                scene.pitch += 360.0f;
        } else {
            const float c[3] = { (float)(vtxList->minCoord[0] + vtxList->maxCoord[0]) * 0.5f,
                                 (float)(vtxList->minCoord[1] + vtxList->maxCoord[1]) * 0.5f,
                                 (float)(vtxList->minCoord[2] + vtxList->maxCoord[2]) * 0.5f };
            scene.eye[0] = c[0];
            scene.eye[1] = c[1] + radius * 0.4f;
            scene.eye[2] = c[2] + radius * 1.1f;
            float dir[3] = { c[0] - scene.eye[0], c[1] - scene.eye[1], c[2] - scene.eye[2] };
            const float dirLen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (dirLen > 1e-3f) {
                dir[0] /= dirLen;
                dir[1] /= dirLen;
                dir[2] /= dirLen;
            }
            scene.yaw = std::atan2(dir[0], -dir[2]) / kDeg;
            scene.pitch = std::asin(dir[1]) / kDeg;
        }
        scene.framed = true;
    }

    EnsureAssetIndexes();
    std::vector<Lightbulb::ModelInstance> insts;
    insts.reserve(mSetup.props.size());
    std::vector<Lightbulb::GizmoInstance> gizmos;
    mPickTargets.clear();
    auto recordGizmoPick = [&](int sel, const Lightbulb::GizmoInstance& giz) {
        PickTarget target;
        target.sel = sel;
        Lightbulb::GizmoBounds(giz, target.min, target.max);
        mPickTargets.push_back(target);
    };
    auto recordPick = [&](int sel, const Lightbulb::ModelInstance& inst, int instIdx) {
        float lo[3], hi[3];
        if (!Lightbulb::DrawnInstanceBounds(instIdx, lo, hi)) {
            const BKVertexList* bounds = modelbin_getVtxList(inst.model);
            if (!bounds) {
                return;
            }
            float localMin[3] = { (float)bounds->minCoord[0], (float)bounds->minCoord[1], (float)bounds->minCoord[2] };
            float localMax[3] = { (float)bounds->maxCoord[0], (float)bounds->maxCoord[1], (float)bounds->maxCoord[2] };
            if (bounds->count > 0) {
                for (int axis = 0; axis < 3; ++axis) {
                    localMin[axis] = (float)bounds->vertices[0].v.ob[axis];
                    localMax[axis] = localMin[axis];
                }
                for (int vert = 1; vert < bounds->count; ++vert) {
                    for (int axis = 0; axis < 3; ++axis) {
                        const float value = (float)bounds->vertices[vert].v.ob[axis];
                        localMin[axis] = std::min(localMin[axis], value);
                        localMax[axis] = std::max(localMax[axis], value);
                    }
                }
            }
            for (int axis = 0; axis < 3; ++axis) {
                lo[axis] = inst.pos[axis] + localMin[axis] * inst.scale;
                hi[axis] = inst.pos[axis] + localMax[axis] * inst.scale;
            }
        }
        PickTarget target;
        target.sel = sel;
        for (int axis = 0; axis < 3; ++axis) {
            target.min[axis] = lo[axis];
            target.max[axis] = hi[axis];
        }
        mPickTargets.push_back(target);
    };

    for (const Lightbulb::SetupProp& prop : mSetup.props) {
        if (prop.type != 2 || !(mConfig.layers & Lightbulb::kLayerModels)) {
            continue;
        }
        const auto found = mModelIndex.find(0x2D1u + prop.id);
        if (found == mModelIndex.end()) {
            continue;
        }
        BKModelBin* pm = Lightbulb::LoadO2rModel(found->second);
        if (!pm) {
            continue;
        }
        Lightbulb::ModelInstance inst;
        inst.model = pm;
        inst.pos[0] = (float)prop.pos[0];
        inst.pos[1] = (float)prop.pos[1];
        inst.pos[2] = (float)prop.pos[2];
        inst.rotDeg[1] = (float)prop.yaw * 2.0f;
        inst.rotDeg[2] = (float)prop.roll * 2.0f;
        inst.scale = prop.scale ? (float)prop.scale / 100.0f : 1.0f;
        insts.push_back(inst);
        recordPick((int)(&prop - mSetup.props.data()), inst, (int)insts.size() - 1);
    }

    std::vector<Lightbulb::SpriteBillboard> sprs;
    const double now = ImGui::GetTime();

    auto emitSprite = [&](const Lightbulb::O2rSpriteTex& spriteTex, const float pos[3], float scale, int phase,
                          int sel) {
        const Lightbulb::SpriteFrame anim =
            mConfig.animateObjects ? Lightbulb::SpriteFrameAt(spriteTex, now, phase)
                                   : Lightbulb::SpriteFrame{ Lightbulb::SpriteRestFrame(spriteTex), false };
        Lightbulb::AppendSpriteBillboards(spriteTex, anim.frame, -1, pos, scale, anim.mirror, false, sprs);

        const float halfWidth = spriteTex.pickHalfWidth * scale;
        if (halfWidth <= 0.0f) {
            return;
        }
        PickTarget target;
        target.sel = sel;
        target.min[0] = pos[0] - halfWidth;
        target.max[0] = pos[0] + halfWidth;
        target.min[1] = pos[1] + spriteTex.pickLowY * scale;
        target.max[1] = pos[1] + spriteTex.pickHighY * scale;
        target.min[2] = pos[2] - halfWidth;
        target.max[2] = pos[2] + halfWidth;
        mPickTargets.push_back(target);
    };

    const uint16_t mapId = scene.entries[scene.sel].mapId;
    Lightbulb::SetAudioListener(scene.eye, mShowMusic ? 0 : mapId, models[0],
                                (int)models.size() > opaqueChunks ? models[opaqueChunks] : nullptr);
    for (const Lightbulb::SetupNode& nd : mSetup.nodes) {
        // A waypoint's position is float bytes read as coordinates, so it has nowhere to draw.
        if (nd.script) {
            continue;
        }
        const int pickSel = (int)mSetup.props.size() + (int)(&nd - mSetup.nodes.data());
        bool drawn = false;
        if (nd.category == 6 && !(mConfig.layers & Lightbulb::kLayerUnregistered) && mRomhackPath.empty() &&
            !Lightbulb::EditorEntryPointId(nd.id) && Lightbulb::ActorIsSpawnable(nd.id) &&
            !Lightbulb::ActorRegisteredForMap(mapId, nd.id)) {
            continue;
        }
        if (nd.category == 6) {
            // With models off the node falls through to the stand-in or gizmo path below.
            const uint32_t assetId = mConfig.actorModels ? Lightbulb::ActorDisplayAsset(nd.id) : 0;
            if (assetId) {
                if (!(mConfig.layers & Lightbulb::kLayerActors)) {
                    continue;
                }
                float scale = nd.scaleRaw ? (float)nd.scaleRaw * 0.01f : 1.0f;
                float drawYOff = 0.0f;
                Lightbulb::ActorDrawTransform(assetId, scale, drawYOff);
                const auto found = mModelIndex.find(assetId);
                if (found != mModelIndex.end()) {
                    BKModelBin* am = Lightbulb::LoadO2rModel(found->second);
                    if (!am) {
                        continue;
                    }
                    Lightbulb::ModelInstance inst;
                    inst.model = am;
                    inst.pos[0] = (float)nd.pos[0];
                    inst.pos[1] = (float)nd.pos[1] + drawYOff;
                    inst.pos[2] = (float)nd.pos[2];
                    const float spin = mConfig.animateObjects ? Lightbulb::ActorSpinRate(nd.id) : 0.0f;
                    float yawDeg = (float)nd.yawRaw;
                    Lightbulb::ActorPlacement(nd.id, inst.pos, yawDeg, scale);
                    inst.scale = scale;
                    inst.rotDeg[1] = spin > 0.0f ? std::fmod(yawDeg + (float)now * spin, 360.0f) : yawDeg;
                    insts.push_back(inst);
                    recordPick(pickSel, inst, (int)insts.size() - 1);
                    drawn = true;

                    if (const uint32_t extra = Lightbulb::ActorExtraModel(nd.id)) {
                        const auto xit = mModelIndex.find(extra);
                        if (xit != mModelIndex.end()) {
                            if (BKModelBin* xm = Lightbulb::LoadO2rModel(xit->second)) {
                                Lightbulb::ModelInstance extraInst;
                                extraInst.model = xm;
                                extraInst.pos[0] = (float)nd.pos[0];
                                extraInst.pos[1] = (float)nd.pos[1];
                                extraInst.pos[2] = (float)nd.pos[2];
                                extraInst.scale = scale;
                                extraInst.rotDeg[1] = (float)nd.yawRaw;
                                insts.push_back(extraInst);
                            }
                        }
                    }
                } else if (const Lightbulb::O2rSpriteTex* spriteTex = Lightbulb::LoadO2rSprite(assetId)) {
                    const float pos[3] = { (float)nd.pos[0], (float)nd.pos[1], (float)nd.pos[2] };
                    emitSprite(*spriteTex, pos, scale, 0, pickSel);
                    drawn = true;
                }
            }
        }
        if (drawn) {
            continue;
        }
        uint32_t layer;
        switch (nd.category) {
            case 3:
                layer = Lightbulb::kLayerWarps;
                break;
            case 4:
                layer = Lightbulb::kLayerCamMarkers;
                break;
            case 6:
                layer = Lightbulb::EditorEntryPointId(nd.id) ? Lightbulb::kLayerEntries : Lightbulb::kLayerActors;
                break;
            case 7:
                layer = Lightbulb::kLayerEnemies;
                break;
            case 8:
                layer = Lightbulb::kLayerPaths;
                break;
            case 9:
                layer = Lightbulb::kLayerTriggers;
                break;
            default:
                layer = Lightbulb::kLayerFlags;
                break;
        }
        if (!(mConfig.layers & layer)) {
            continue;
        }
        if (const char* standIn = Lightbulb::EditorStandInModel(nd.category, nd.id)) {
            if (BKModelBin* sm = Lightbulb::LoadO2rModel(standIn)) {
                Lightbulb::ModelInstance inst;
                inst.model = sm;
                inst.pos[0] = (float)nd.pos[0];
                inst.pos[1] = (float)nd.pos[1];
                inst.pos[2] = (float)nd.pos[2];
                if (Lightbulb::EditorStandInInsidePole(nd.category, nd.id)) {
                    inst.rotDeg[1] = std::atan2(scene.eye[0] - inst.pos[0], scene.eye[2] - inst.pos[2]) / kDeg;
                    float toEye[3] = { scene.eye[0] - inst.pos[0], scene.eye[1] - inst.pos[1],
                                       scene.eye[2] - inst.pos[2] };
                    const float dist = std::sqrt(toEye[0] * toEye[0] + toEye[1] * toEye[1] + toEye[2] * toEye[2]);
                    if (dist > 1.0f) {
                        const float clear = std::min((nd.radius ? (float)nd.radius : 50.0f) + 10.0f, dist * 0.5f);
                        for (int axis = 0; axis < 3; ++axis) {
                            inst.pos[axis] += toEye[axis] / dist * clear;
                        }
                    }
                } else {
                    inst.rotDeg[1] = (float)nd.yawRaw;
                }
                insts.push_back(inst);
                recordPick(pickSel, inst, (int)insts.size() - 1);
                continue;
            }
        }
        Lightbulb::GizmoInstance giz;
        giz.kind = nd.category == 9 ? Lightbulb::GIZMO_CUBE : Lightbulb::GIZMO_PYRAMID;
        giz.pos[0] = (float)nd.pos[0];
        giz.pos[1] = (float)nd.pos[1];
        giz.pos[2] = (float)nd.pos[2];
        if (nd.category == 9) {
            setRareCameraColor(giz.color, nd.id);
        } else {
            giz.color[0] = giz.color[1] = giz.color[2] = 230;
        }
        gizmos.push_back(giz);
        recordGizmoPick(pickSel, giz);
    }

    for (const Lightbulb::SetupCamera& cam : mSetup.cameras) {
        if (!(mConfig.layers & Lightbulb::kLayerCameras)) {
            break;
        }
        Lightbulb::GizmoInstance giz;
        giz.kind = Lightbulb::GIZMO_CAMERA;
        setRareCameraColor(giz.color, cam.index);
        giz.pos[0] = cam.pos[0];
        giz.pos[1] = cam.pos[1];
        giz.pos[2] = cam.pos[2];
        if (cam.type == 2) {
            giz.pitchYawRollDeg[0] = cam.pitchYawRoll[0];
            giz.pitchYawRollDeg[1] = cam.pitchYawRoll[1];
            giz.pitchYawRollDeg[2] = cam.pitchYawRoll[2];
        }
        gizmos.push_back(giz);
        recordGizmoPick((int)mSetup.props.size() + (int)mSetup.nodes.size() + (int)(&cam - mSetup.cameras.data()), giz);
    }

    if (mConfig.layers & Lightbulb::kLayerBoundary) {
        Lightbulb::GizmoInstance giz;
        giz.kind = Lightbulb::GIZMO_WIREBOX;
        for (int axis = 0; axis < 3; ++axis) {
            const float lo = (float)(mSetup.boundsMin[axis] * 1000);
            const float hi = (float)((mSetup.boundsMax[axis] + 1) * 1000);
            giz.pos[axis] = (lo + hi) * 0.5f;
            giz.halfExtent[axis] = (hi - lo) * 0.5f;
        }
        gizmos.push_back(giz);
    }

    const int selNode = mPropSel - (int)mSetup.props.size();
    if ((mConfig.layers & Lightbulb::kLayerRadius) && selNode >= 0 && selNode < (int)mSetup.nodes.size() &&
        mSetup.nodes[selNode].radius > 0) {
        const Lightbulb::SetupNode& nd = mSetup.nodes[selNode];
        Lightbulb::GizmoInstance giz;
        giz.kind = Lightbulb::GIZMO_SPHERE;
        giz.pos[0] = (float)nd.pos[0];
        giz.pos[1] = (float)nd.pos[1];
        giz.pos[2] = (float)nd.pos[2];
        giz.radius = (float)nd.radius;
        switch (nd.category) {
            case 7:
                giz.color[0] = 255;
                giz.color[1] = 0;
                giz.color[2] = 0;
                break;
            case 9:
                setRareCameraColor(giz.color, nd.id);
                break;
            case 10:
                giz.color[0] = 0;
                giz.color[1] = 255;
                giz.color[2] = 0;
                break;
            default:
                break;
        }
        gizmos.push_back(giz);
    }

    for (const Lightbulb::SetupProp& prop : mSetup.props) {
        if (prop.type != 0 || !(mConfig.layers & Lightbulb::kLayerSprites)) {
            continue;
        }
        const Lightbulb::O2rSpriteTex* spriteTex = Lightbulb::LoadO2rSprite(0x572u + prop.id);
        if (!spriteTex) {
            continue;
        }
        const float pos[3] = { (float)prop.pos[0], (float)prop.pos[1], (float)prop.pos[2] };
        emitSprite(*spriteTex, pos, prop.scale ? (float)prop.scale / 100.0f : 1.0f, prop.spritePhase,
                   (int)(&prop - mSetup.props.data()));
    }

    {
        Lightbulb::MapExtraModel extras[3];
        const int extraCount = Lightbulb::MapExtraModels(scene.entries[scene.sel].mapId, extras);
        for (int extra = 0; extra < extraCount; ++extra) {
            const auto found = mModelIndex.find(extras[extra].modelId);
            if (found == mModelIndex.end()) {
                continue;
            }
            if (BKModelBin* extraModel = Lightbulb::LoadO2rModel(found->second)) {
                Lightbulb::ModelInstance inst;
                inst.model = extraModel;
                inst.pos[0] = extras[extra].pos[0];
                inst.pos[1] = extras[extra].pos[1];
                inst.pos[2] = extras[extra].pos[2];
                inst.scale = extras[extra].scale;
                insts.push_back(inst);
            }
        }
    }

    Lightbulb::SkyLayerInfo skyInfo[3];
    const int skyCount = Lightbulb::SkyLayersForMap(scene.entries[scene.sel].mapId, skyInfo);
    std::vector<Lightbulb::SkyLayer> skyLayers;
    skyLayers.reserve(skyCount);
    for (int layer = 0; layer < skyCount; ++layer) {
        const auto found = mModelIndex.find(skyInfo[layer].modelId);
        if (found == mModelIndex.end()) {
            continue;
        }
        BKModelBin* skyModel = Lightbulb::LoadO2rModel(found->second);
        if (!skyModel) {
            continue;
        }
        Lightbulb::SkyLayer skyLayer;
        skyLayer.model = skyModel;
        skyLayer.rotYDeg = std::fmod(skyInfo[layer].rotSpeed * (float)now, 360.0f);
        skyLayer.scale = skyInfo[layer].scale;
        skyLayers.push_back(skyLayer);
    }

    Lightbulb::ModelDrawParams drawParams;
    drawParams.freeFly = true;
    for (const PickTarget& target : mPickTargets) {
        if (target.sel != mPropSel) {
            continue;
        }
        drawParams.selectionValid = true;
        for (int axis = 0; axis < 3; ++axis) {
            drawParams.selectionMin[axis] = target.min[axis];
            drawParams.selectionMax[axis] = target.max[axis];
        }
        break;
    }
    drawParams.animTime = (float)ImGui::GetTime();
    drawParams.eye[0] = scene.eye[0];
    drawParams.eye[1] = scene.eye[1];
    drawParams.eye[2] = scene.eye[2];
    drawParams.lookYawDeg = scene.yaw;
    drawParams.lookPitchDeg = scene.pitch;
    drawParams.nearOverride = 10.0f;
    drawParams.farOverride = 200000.0f;
    drawParams.fovYDeg = kLevelFovYDeg;
    drawParams.opaqueModelCount = opaqueChunks;
    drawParams.sky = skyLayers.empty() ? nullptr : skyLayers.data();
    drawParams.skyCount = (int)skyLayers.size();
    drawParams.gizmos = gizmos.empty() ? nullptr : gizmos.data();
    drawParams.gizmoCount = (int)gizmos.size();
    return Lightbulb::RenderModelsAsGameFrame(models.data(), (int)models.size(), drawParams,
                                              insts.empty() ? nullptr : insts.data(), (int)insts.size(),
                                              sprs.empty() ? nullptr : sprs.data(), (int)sprs.size());
}
