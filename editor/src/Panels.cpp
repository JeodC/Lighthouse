#include "App.h"
#include "O2rImport.h"

#include "PreviewScene.h"
#include "UiCommon.h"
extern "C" {
#include "model.h"
}

#include "imgui.h"
#include "imgui_internal.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr float kDeg = 3.14159265f / 180.0f;

// Shared by the level projection and the click ray; they must not drift apart.
constexpr float kLevelFovYDeg = 55.0f;

std::map<uint32_t, std::string> indexById(const std::vector<std::string>& paths) {
    std::map<uint32_t, std::string> idx;
    for (const std::string& path : paths) {
        const size_t slash = path.find_last_of('/');
        const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
        if (std::strncmp(name, "ASSET_", 6) == 0) {
            idx.emplace(static_cast<uint32_t>(std::strtoul(name + 6, nullptr, 16)), path);
        }
    }
    return idx;
}

float rayHitsBox(const float origin[3], const float dir[3], const float lo[3], const float hi[3]) {
    float tMin = 0.0f;
    float tMax = 1e30f;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dir[axis]) < 1e-6f) {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
                return -1.0f;
            }
            continue;
        }
        const float inv = 1.0f / dir[axis];
        float near = (lo[axis] - origin[axis]) * inv;
        float far = (hi[axis] - origin[axis]) * inv;
        if (near > far) {
            const float swap = near;
            near = far;
            far = swap;
        }
        if (near > tMin) {
            tMin = near;
        }
        if (far < tMax) {
            tMax = far;
        }
        if (tMin > tMax) {
            return -1.0f;
        }
    }
    return tMin;
}

std::string assetFullName(const std::map<uint32_t, std::string>& index, uint32_t assetId) {
    const auto found = index.find(assetId);
    if (found == index.end()) {
        char fallback[32];
        std::snprintf(fallback, sizeof(fallback), "ASSET_%X (missing)", assetId);
        return fallback;
    }
    const size_t slash = found->second.find_last_of('/');
    return slash == std::string::npos ? found->second : found->second.substr(slash + 1);
}

std::string assetShortName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (base.rfind("ASSET_", 0) == 0) {
        const size_t underscore = base.find('_', 6);
        if (underscore != std::string::npos && underscore + 1 < base.size()) {
            const std::string tail = base.substr(underscore + 1);
            if (tail == "UNNAMED") {
                return base.substr(6, underscore - 6);
            }
            return tail;
        }
    }
    return base;
}

} // namespace

void App::DrawLevelsPanel() {
    if (!ImGui::Begin("Levels")) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded) {
        ImGui::TextWrapped("Open a bk.o2r to browse and fly through its levels.");
        ImGui::End();
        return;
    }

    LevelScene& scene = mLevelScene;

    if (scene.entries.empty()) {
        const std::map<uint32_t, std::string> levelIdx = indexById(Lightbulb::ListO2rModelPaths("assets/level"));
        const std::map<uint32_t, std::string> setupIdx = indexById(Lightbulb::ListO2rResourcePaths("setup"));
        for (int index = 0; index < Lightbulb::kBKLevelCount; ++index) {
            const Lightbulb::BKLevel& level = Lightbulb::kBKLevels[index];
            const auto opaque = levelIdx.find(level.opaModel);
            if (opaque == levelIdx.end()) {
                continue;
            }
            LevelEntry entry;
            entry.name = level.name;
            entry.mapId = level.mapId;
            entry.chunks.push_back(opaque->second);
            if (level.xluModel) {
                const auto translucent = levelIdx.find(level.xluModel);
                if (translucent != levelIdx.end()) {
                    entry.chunks.push_back(translucent->second);
                }
            }
            const auto setup = setupIdx.find(Lightbulb::BKLevelSetupAsset(level));
            if (setup != setupIdx.end()) {
                entry.setupPath = setup->second;
            }
            scene.entries.push_back(std::move(entry));
        }
    }

    const float availY = ImGui::GetContentRegionAvail().y;
    if (ImGui::CollapsingHeader("Levels", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("##levellist", ImVec2(0, availY * 0.4f), true);
        for (int row = 0; row < (int)scene.entries.size(); ++row) {
            char label[208];
            std::snprintf(label, sizeof(label), "%s##lvl%d", scene.entries[row].name.c_str(), row);
            if (ImGui::Selectable(label, row == scene.sel)) {
                scene.sel = row;
                scene.framed = false;
                mPropSel = -1;
                if (!scene.entries[row].setupPath.empty()) {
                    Lightbulb::LoadO2rSetup(scene.entries[row].setupPath, mSetup);
                } else {
                    mSetup = Lightbulb::SetupScene{};
                }
            }
        }
        ImGui::EndChild();
    }

    if (mSetup.loaded && ImGui::CollapsingHeader("Entry points")) {
        const int propCount = (int)mSetup.props.size();
        std::vector<int> entryIdx;
        for (int idx = 0; idx < (int)mSetup.nodes.size(); ++idx) {
            const Lightbulb::SetupNode& nd = mSetup.nodes[idx];
            if (nd.category == 6 && Lightbulb::EditorEntryPointId(nd.id)) {
                entryIdx.push_back(idx);
            }
        }
        const int rows = entryIdx.empty() ? 1 : ((int)entryIdx.size() < 5 ? (int)entryIdx.size() : 5);
        ImGui::BeginChild("##entrylist", ImVec2(0, rows * ImGui::GetTextLineHeightWithSpacing() + 12.0f), true);
        for (int idx : entryIdx) {
            const Lightbulb::SetupNode& nd = mSetup.nodes[idx];
            char label[96];
            std::snprintf(label, sizeof(label), "Entry %X   (%d, %d, %d)##entry%d", nd.id, nd.pos[0], nd.pos[1],
                          nd.pos[2], idx);
            if (ImGui::Selectable(label, mPropSel == propCount + idx)) {
                mPropSel = propCount + idx;
                scene.eye[0] = (float)nd.pos[0];
                scene.eye[1] = (float)nd.pos[1] + 50.0f;
                scene.eye[2] = (float)nd.pos[2] + 500.0f;
                scene.yaw = 0.0f;
                scene.pitch = 0.0f;
            }
        }
        if (entryIdx.empty()) {
            Lightbulb::ui::TextDisabledWrapped("No entry points in this setup.");
        }
        ImGui::EndChild();
    }

    if (ImGui::BeginTabBar("##lefttabs")) {
        if (ImGui::BeginTabItem("Objects")) {
            DrawObjectsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cameras")) {
            DrawCamerasTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Paths")) {
            DrawPathsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (scene.sel >= 0 && scene.framed) {
        ImGuiIO& io = ImGui::GetIO();
        float viewX, viewY, viewW, viewH;
        const ImGuiWindow* gameWindow = ImGui::FindWindowByName("Main Game");
        const bool overView =
            Lightbulb::GetGameViewportRect(viewX, viewY, viewW, viewH) &&
            ImGui::IsMouseHoveringRect(ImVec2(viewX, viewY), ImVec2(viewX + viewW, viewY + viewH), false) &&
            gameWindow != nullptr && ImGui::GetCurrentContext()->HoveredWindow == gameWindow;
        if (overView && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            scene.looking = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            scene.looking = false;
        }
        if (overView && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            const float cosPitch = std::cos(scene.pitch * kDeg);
            const float forward[3] = { cosPitch * std::sin(scene.yaw * kDeg), std::sin(scene.pitch * kDeg),
                                       -cosPitch * std::cos(scene.yaw * kDeg) };
            const float right[3] = { std::cos(scene.yaw * kDeg), 0.0f, std::sin(scene.yaw * kDeg) };
            const float up[3] = { right[1] * forward[2] - right[2] * forward[1],
                                  right[2] * forward[0] - right[0] * forward[2],
                                  right[0] * forward[1] - right[1] * forward[0] };
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float ndcX = ((mouse.x - viewX) / viewW) * 2.0f - 1.0f;
            const float ndcY = 1.0f - ((mouse.y - viewY) / viewH) * 2.0f;
            const float tanHalf = std::tan(kLevelFovYDeg * 0.5f * kDeg);
            const float aspect = viewW / viewH;
            float dir[3];
            for (int axis = 0; axis < 3; ++axis) {
                dir[axis] = forward[axis] + right[axis] * ndcX * tanHalf * aspect + up[axis] * ndcY * tanHalf;
            }
            const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (len > 1e-6f) {
                dir[0] /= len;
                dir[1] /= len;
                dir[2] /= len;
            }
            int best = -1;
            float bestVolume = 0.0f;
            float bestDist = 0.0f;
            for (const PickTarget& target : mPickTargets) {
                const float hit = rayHitsBox(scene.eye, dir, target.min, target.max);
                if (hit < 0.0f) {
                    continue;
                }
                float volume = 1.0f;
                for (int axis = 0; axis < 3; ++axis) {
                    volume *= std::max(1.0f, target.max[axis] - target.min[axis]);
                }
                if (best < 0 || volume < bestVolume || (volume == bestVolume && hit < bestDist)) {
                    bestVolume = volume;
                    bestDist = hit;
                    best = target.sel;
                }
            }
            mPropSel = best;
        }
        if (scene.looking && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            scene.yaw += io.MouseDelta.x * 0.25f;
            scene.pitch -= io.MouseDelta.y * 0.25f;
            scene.pitch = scene.pitch > 89.0f ? 89.0f : (scene.pitch < -89.0f ? -89.0f : scene.pitch);
        }
        if (overView && !io.WantTextInput) {
            const float cosPitch = std::cos(scene.pitch * kDeg);
            const float look[3] = { cosPitch * std::sin(scene.yaw * kDeg), std::sin(scene.pitch * kDeg),
                                    -cosPitch * std::cos(scene.yaw * kDeg) };
            const float right[3] = { std::cos(scene.yaw * kDeg), 0.0f, std::sin(scene.yaw * kDeg) };
            const float step = mConfig.cameraSpeed * io.DeltaTime * 60.0f;
            auto move = [&](const float axis[3], float amount) {
                scene.eye[0] += axis[0] * amount;
                scene.eye[1] += axis[1] * amount;
                scene.eye[2] += axis[2] * amount;
            };
            if (ImGui::IsKeyDown(ImGuiKey_W))
                move(look, step);
            if (ImGui::IsKeyDown(ImGuiKey_S))
                move(look, -step);
            if (ImGui::IsKeyDown(ImGuiKey_D))
                move(right, step);
            if (ImGui::IsKeyDown(ImGuiKey_A))
                move(right, -step);
            if (ImGui::IsKeyDown(ImGuiKey_E))
                scene.eye[1] += step;
            if (ImGui::IsKeyDown(ImGuiKey_Q))
                scene.eye[1] -= step;
        }
    }

    ImGui::End();
}

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
                opaqueChunks = (int)models.size();
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
            if (nd.category == 6 && Lightbulb::EditorEntryPointId(nd.id) && (!entry || nd.id < entry->id)) {
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
            scene.eye[0] = (float)entry->pos[0];
            scene.eye[1] = (float)entry->pos[1] + 50.0f;
            scene.eye[2] = (float)entry->pos[2] + 500.0f;
            scene.yaw = 0.0f;
            scene.pitch = 0.0f;
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

    if (mModelIndex.empty()) {
        mModelIndex = indexById(Lightbulb::ListO2rModelPaths("assets/model"));
        mSpriteIndex = indexById(Lightbulb::ListO2rSpritePaths());
    }
    std::vector<Lightbulb::ModelInstance> insts;
    insts.reserve(mSetup.props.size());
    std::vector<Lightbulb::GizmoInstance> gizmos;
    mPickTargets.clear();
    // Props, nodes and cameras share one selection index space, in that order, so a
    // row in any panel and a click in the viewport resolve to the same object.
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
        const Lightbulb::SpriteFrame anim = Lightbulb::SpriteFrameAt(spriteTex, now, phase);
        Lightbulb::AppendSpriteBillboards(spriteTex, anim.frame, -1, pos, scale, anim.mirror, false, sprs);

        static std::vector<Lightbulb::SpriteBillboard> allFrames;
        allFrames.clear();
        for (int frame = 0; frame < (int)spriteTex.frames.size(); ++frame) {
            Lightbulb::AppendSpriteBillboards(spriteTex, frame, -1, pos, scale, false, false, allFrames);
        }
        float halfWidth = 0.0f;
        float lowY = 0.0f;
        float highY = 0.0f;
        bool any = false;
        for (const Lightbulb::SpriteBillboard& billboard : allFrames) {
            halfWidth = std::max(halfWidth, 0.5f * std::fabs(billboard.x1 - billboard.x0));
            lowY = any ? std::min(lowY, billboard.y0) : billboard.y0;
            highY = any ? std::max(highY, billboard.y1) : billboard.y1;
            any = true;
        }
        if (!any || halfWidth <= 0.0f) {
            return;
        }
        PickTarget target;
        target.sel = sel;
        target.min[0] = pos[0] - halfWidth;
        target.max[0] = pos[0] + halfWidth;
        target.min[1] = pos[1] + lowY;
        target.max[1] = pos[1] + highY;
        target.min[2] = pos[2] - halfWidth;
        target.max[2] = pos[2] + halfWidth;
        mPickTargets.push_back(target);
    };

    const uint16_t mapId = scene.entries[scene.sel].mapId;
    for (const Lightbulb::SetupNode& nd : mSetup.nodes) {
        const int pickSel = (int)mSetup.props.size() + (int)(&nd - mSetup.nodes.data());
        bool drawn = false;
        if (nd.category == 6 && !(mConfig.layers & Lightbulb::kLayerUnregistered) && mRomhackPath.empty() &&
            !Lightbulb::EditorEntryPointId(nd.id) && Lightbulb::ActorIsSpawnable(nd.id) &&
            !Lightbulb::ActorRegisteredForMap(mapId, nd.id)) {
            continue;
        }
        if (nd.category == 6) {
            const uint32_t assetId = Lightbulb::ActorDisplayAsset(nd.id);
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
                    inst.scale = scale;
                    const float spin = Lightbulb::ActorSpinRate(nd.id);
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
                                Lightbulb::ModelInstance xluIdx;
                                xluIdx.model = xm;
                                xluIdx.pos[0] = (float)nd.pos[0];
                                xluIdx.pos[1] = (float)nd.pos[1];
                                xluIdx.pos[2] = (float)nd.pos[2];
                                xluIdx.scale = scale;
                                xluIdx.rotDeg[1] = (float)nd.yawRaw;
                                insts.push_back(xluIdx);
                            }
                        }
                    }
                } else {
                    Lightbulb::O2rSpriteTex spriteTex;
                    if (Lightbulb::LoadO2rSprite(assetId, spriteTex)) {
                        const float pos[3] = { (float)nd.pos[0], (float)nd.pos[1], (float)nd.pos[2] };
                        emitSprite(spriteTex, pos, scale, 0, pickSel);
                        drawn = true;
                    }
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
                inst.rotDeg[1] = (float)nd.yawRaw;
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
            giz.color[0] = 0;
            giz.color[1] = 0;
            giz.color[2] = 255;
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

    // The setup stores the boundary as cube-grid indices; each cube is 1000 units
    // and max is inclusive, so the far corner is (max + 1) * 1000.
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
                giz.color[0] = 0;
                giz.color[1] = 0;
                giz.color[2] = 255;
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
        Lightbulb::O2rSpriteTex spriteTex;
        if (!Lightbulb::LoadO2rSprite(0x572u + prop.id, spriteTex)) {
            continue;
        }
        const float pos[3] = { (float)prop.pos[0], (float)prop.pos[1], (float)prop.pos[2] };
        emitSprite(spriteTex, pos, prop.scale ? (float)prop.scale / 100.0f : 1.0f, prop.spritePhase,
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

void App::DrawObjectsTab() {
    if (!mSetup.loaded) {
        ImGui::TextWrapped("Select a level to load its objects.");
        return;
    }

    if (mModelIndex.empty()) {
        mModelIndex = indexById(Lightbulb::ListO2rModelPaths("assets/model"));
        mSpriteIndex = indexById(Lightbulb::ListO2rSpritePaths());
    }
    ImGui::Text("cubes %d   objects %d   spawns/warps/triggers %d   cameras %d", mSetup.cubeCount,
                (int)mSetup.props.size(), (int)mSetup.nodes.size(), (int)mSetup.cameras.size());
    ImGui::Separator();
    if (ImGui::BeginChild("##o2robjs")) {
        if (ImGui::BeginTable("##props", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 54.0f);
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (int row = 0; row < (int)mSetup.props.size(); ++row) {
                const Lightbulb::SetupProp& prop = mSetup.props[row];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char sel[32];
                std::snprintf(sel, sizeof(sel), "%d##prop%d", row, row);
                if (ImGui::Selectable(sel, row == mPropSel, ImGuiSelectableFlags_SpanAllColumns)) {
                    mPropSel = row;
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled(prop.type == 2 ? "model" : prop.type == 0 ? "sprite" : "actor");
                ImGui::TableNextColumn();
                if (prop.type == 2) {
                    const auto found = mModelIndex.find(0x2D1u + prop.id);
                    if (found != mModelIndex.end()) {
                        ImGui::TextUnformatted(assetShortName(found->second).c_str());
                    } else {
                        ImGui::TextDisabled("model %X (missing)", 0x2D1 + prop.id);
                    }
                } else if (prop.type == 0) {
                    const auto found = mSpriteIndex.find(0x572u + prop.id);
                    if (found != mSpriteIndex.end()) {
                        ImGui::TextUnformatted(assetShortName(found->second).c_str());
                    } else {
                        ImGui::TextDisabled("sprite %X (missing)", 0x572 + prop.id);
                    }
                } else if (const char* an = Lightbulb::ActorEnumName(prop.id)) {
                    ImGui::TextUnformatted(an);
                } else {
                    ImGui::TextDisabled("actor %X", prop.id);
                }
            }

            const int propCount = (int)mSetup.props.size();
            for (int nodeRow = 0; nodeRow < (int)mSetup.nodes.size(); ++nodeRow) {
                const Lightbulb::SetupNode& node = mSetup.nodes[nodeRow];
                const int row = propCount + nodeRow;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char sel[32];
                std::snprintf(sel, sizeof(sel), "%d##node%d", row, nodeRow);
                if (ImGui::Selectable(sel, row == mPropSel, ImGuiSelectableFlags_SpanAllColumns)) {
                    mPropSel = row;
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled(node.category == 6 ? "spawn" : "node");
                ImGui::TableNextColumn();
                const char* actorName = node.category == 6 ? Lightbulb::ActorEnumName(node.id) : nullptr;
                if (actorName) {
                    ImGui::TextUnformatted(actorName);
                } else {
                    ImGui::TextDisabled("id %X (cat %u)", node.id, (unsigned)node.category);
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void App::DrawCamerasTab() {
    if (!mSetup.loaded) {
        ImGui::TextWrapped("Select a level to see its cameras.");
        return;
    }

    ImGui::Text("%d cameras", (int)mSetup.cameras.size());
    ImGui::Separator();
    if (ImGui::BeginChild("##camlist")) {
        if (ImGui::BeginTable("##cams", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            const int camBase = (int)mSetup.props.size() + (int)mSetup.nodes.size();
            for (const Lightbulb::SetupCamera& c : mSetup.cameras) {
                const int row = camBase + (int)(&c - mSetup.cameras.data());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char sel[32];
                std::snprintf(sel, sizeof(sel), "%d##cam%d", (int)c.index, row);
                if (ImGui::Selectable(sel, row == mPropSel, ImGuiSelectableFlags_SpanAllColumns)) {
                    mPropSel = row;
                }
                ImGui::TableNextColumn();
                static const char* kCamType[] = { "?", "pivot", "static", "zoom" };
                ImGui::TextDisabled("%s (%u)", c.type < 4 ? kCamType[c.type] : "?", (unsigned)c.type);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f, %.0f, %.0f", c.pos[0], c.pos[1], c.pos[2]);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void App::DrawPathsTab() {
    if (!mSetup.loaded) {
        ImGui::TextWrapped("Select a level to see its paths.");
        return;
    }
    const int propCount = (int)mSetup.props.size();
    int pathNodes = 0;
    for (const Lightbulb::SetupNode& nd : mSetup.nodes) {
        if (nd.category == 8) {
            ++pathNodes;
        }
    }
    ImGui::Text("%d path nodes", pathNodes);
    ImGui::Separator();
    if (pathNodes == 0) {
        Lightbulb::ui::TextDisabledWrapped("No path nodes in this setup.");
        return;
    }
    if (ImGui::BeginChild("##pathlist")) {
        if (ImGui::BeginTable("##paths", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Next", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (int idx = 0; idx < (int)mSetup.nodes.size(); ++idx) {
                const Lightbulb::SetupNode& nd = mSetup.nodes[idx];
                if (nd.category != 8) {
                    continue;
                }
                const int row = propCount + idx;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char sel[32];
                std::snprintf(sel, sizeof(sel), "%d##path%d", idx, idx);
                if (ImGui::Selectable(sel, row == mPropSel, ImGuiSelectableFlags_SpanAllColumns)) {
                    mPropSel = row;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u", (unsigned)nd.pathUid);
                ImGui::TableNextColumn();
                if (nd.pathNext) {
                    ImGui::Text("%u", (unsigned)nd.pathNext);
                } else {
                    ImGui::TextDisabled("end");
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d, %d, %d", nd.pos[0], nd.pos[1], nd.pos[2]);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void App::DrawToolbar() {
    if (!mO2rLoaded) {
        return;
    }
    const ImGuiID dockId = ImHashStr("main_dock", 0, ImHashStr("Main - Deck"));
    if (const ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockId)) {
        if (root->ChildNodes[0]) {
            ImGui::SetNextWindowDockID(root->ChildNodes[0]->ID, ImGuiCond_FirstUseEver);
        }
    }
    if (!ImGui::Begin("Controls")) {
        ImGui::End();
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Fly: WASD / QE, left-drag to look, right-click to select");
    ImGui::SameLine(ImGui::GetWindowWidth() - 270.0f);
    ImGui::SetNextItemWidth(150.0f);
    int moveSpeed = (int)(mConfig.cameraSpeed / 10.0f + 0.5f) * 10;
    if (ImGui::SliderInt("move speed", &moveSpeed, 10, 100, "%d")) {
        mConfig.cameraSpeed = (float)((moveSpeed / 10) * 10);
        SaveSettings();
    }
    ImGui::End();
}

void App::DrawLayersPanel() {
    if (!ImGui::Begin("Layers")) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded || !mSetup.loaded) {
        ImGui::TextWrapped("Select a level to toggle its object layers.");
        ImGui::End();
        return;
    }
    static const struct {
        const char* name;
        uint32_t bit;
    } kRows[] = {
        { "Models", Lightbulb::kLayerModels },
        { "Sprites", Lightbulb::kLayerSprites },
        { "Actors", Lightbulb::kLayerActors },
        { "Unregistered actors", Lightbulb::kLayerUnregistered },
        { "Entry points", Lightbulb::kLayerEntries },
        { "Warps", Lightbulb::kLayerWarps },
        { "Camera path triggers", Lightbulb::kLayerCamMarkers },
        { "Enemy boundaries", Lightbulb::kLayerEnemies },
        { "Path nodes", Lightbulb::kLayerPaths },
        { "Camera triggers", Lightbulb::kLayerTriggers },
        { "Flags / markers", Lightbulb::kLayerFlags },
        { "Cameras", Lightbulb::kLayerCameras },
        { "Radius spheres", Lightbulb::kLayerRadius },
        { "Level boundary", Lightbulb::kLayerBoundary },
    };
    bool changed = false;
    if (ImGui::SmallButton("All")) {
        mConfig.layers = 0xFFFFFFFFu;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        mConfig.layers = 0;
        changed = true;
    }
    ImGui::Separator();
    for (const auto& row : kRows) {
        changed |= ImGui::CheckboxFlags(row.name, &mConfig.layers, row.bit);
        if (row.bit == Lightbulb::kLayerUnregistered && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show actor spawns this map's spawn queues never register. Vanilla only, romhacks unaffected.");
        }
    }
    if (changed) {
        SaveSettings();
    }
    ImGui::End();
}

void App::DrawPropertiesPanel() {
    if (!ImGui::Begin("Properties")) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded || !mSetup.loaded) {
        ImGui::TextWrapped("Select a level, then pick an object in the Objects panel.");
        ImGui::End();
        return;
    }
    if (mModelIndex.empty()) {
        mModelIndex = indexById(Lightbulb::ListO2rModelPaths("assets/model"));
        mSpriteIndex = indexById(Lightbulb::ListO2rSpritePaths());
    }
    if (ImGui::BeginTabBar("##proptabs")) {
        if (ImGui::BeginTabItem("Selection")) {
            DrawSelectionProperties();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Level")) {
            DrawLevelProperties();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void App::DrawSelectionProperties() {
    const int propCount = (int)mSetup.props.size();
    const int nodeCount = (int)mSetup.nodes.size();
    if (mPropSel < 0 || mPropSel >= propCount + nodeCount + (int)mSetup.cameras.size()) {
        Lightbulb::ui::TextDisabledWrapped("Select an object in the Objects panel.");
        return;
    }

    if (mPropSel >= propCount + nodeCount) {
        const Lightbulb::SetupCamera& cam = mSetup.cameras[mPropSel - propCount - nodeCount];
        static const char* kCamType[] = { "?", "Pivot", "Static", "Zoom" };
        ImGui::Text("Camera #%d", (int)cam.index);
        ImGui::Separator();
        ImGui::Text("Type        : %s (%u)", cam.type < 4 ? kCamType[cam.type] : "?", (unsigned)cam.type);
        ImGui::Text("Position    : %.0f, %.0f, %.0f", cam.pos[0], cam.pos[1], cam.pos[2]);
        ImGui::Text("Pitch       : %.1f", cam.pitchYawRoll[0]);
        ImGui::Text("Yaw         : %.1f", cam.pitchYawRoll[1]);
        ImGui::Text("Roll        : %.1f", cam.pitchYawRoll[2]);
        return;
    }

    if (mPropSel >= propCount) {
        const Lightbulb::SetupNode& node = mSetup.nodes[mPropSel - propCount];
        const bool isSpawn = node.category == 6;
        const char* actorName = isSpawn ? Lightbulb::ActorEnumName(node.id) : nullptr;
        ImGui::Text("Node #%d", mPropSel - propCount);
        ImGui::Separator();
        ImGui::Text("Kind        : %s", isSpawn ? "Actor spawn" : "Node");
        if (isSpawn) {
            ImGui::Text("Actor       : %s", actorName ? actorName : "(unnamed)");
        }
        ImGui::Text("Id          : %X", node.id);
        if (isSpawn && !Lightbulb::EditorEntryPointId(node.id) && Lightbulb::ActorIsSpawnable(node.id) &&
            mRomhackPath.empty() && mLevelScene.sel >= 0 && mLevelScene.sel < (int)mLevelScene.entries.size() &&
            !Lightbulb::ActorRegisteredForMap(mLevelScene.entries[mLevelScene.sel].mapId, node.id)) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f), "Never spawns  : map doesn't register this actor");
        }
        const uint32_t modelAsset = isSpawn ? Lightbulb::ActorModelAsset(node.id) : 0;
        if (modelAsset) {
            ImGui::Text("Model asset : %s", assetFullName(mModelIndex, modelAsset).c_str());
        }
        ImGui::Text("Category    : %u", (unsigned)node.category);
        ImGui::Text("Position    : %d, %d, %d", node.pos[0], node.pos[1], node.pos[2]);
        ImGui::Text("Radius      : %u", (unsigned)node.radius);
        ImGui::Text("Yaw (raw)   : %u", (unsigned)node.yawRaw);
        ImGui::Text("Scale (raw) : %u", (unsigned)node.scaleRaw);
        return;
    }

    const Lightbulb::SetupProp& prop = mSetup.props[mPropSel];
    static const char* kType[] = { "Sprite", "Actor", "Model" };
    ImGui::Text("Placed object #%d", mPropSel);
    ImGui::Separator();
    ImGui::Text("Type        : %s", prop.type < 3 ? kType[prop.type] : "?");
    if (prop.type == 2) {
        ImGui::Text("Model asset : %s", assetFullName(mModelIndex, 0x2D1u + prop.id).c_str());
    } else if (prop.type == 0) {
        ImGui::Text("Sprite asset: %s", assetFullName(mSpriteIndex, 0x572u + prop.id).c_str());
    } else {
        ImGui::Text("Actor id    : %X", prop.id);
    }
    ImGui::Text("Asset index : %u", (unsigned)prop.id);
    ImGui::Text("Position    : %d, %d, %d", prop.pos[0], prop.pos[1], prop.pos[2]);
    ImGui::Text("Yaw         : %u", (unsigned)prop.yaw);
    ImGui::Text("Roll        : %u", (unsigned)prop.roll);
    ImGui::Text("Scale       : %u", (unsigned)prop.scale);
    ImGui::Text("Flags       : 0x%02X", (unsigned)prop.flags);
    ImGui::Spacing();
    Lightbulb::ui::TextDisabledWrapped("Editing placed objects (move / properties / add / delete) and writing "
                                       "the setup back to the o2r is coming next.");
}

void App::DrawLevelProperties() {
    if (mLevelScene.sel >= 0 && mLevelScene.sel < (int)mLevelScene.entries.size()) {
        const LevelEntry& level = mLevelScene.entries[mLevelScene.sel];
        ImGui::Text("%s", level.name.c_str());
        ImGui::Separator();
        ImGui::Text("Map id      : %X", (unsigned)level.mapId);
        ImGui::Text("Chunks      : %d", (int)level.chunks.size());
        ImGui::Text("Cubes       : %d", mSetup.cubeCount);
        ImGui::Text("Objects     : %d + %d nodes", (int)mSetup.props.size(), (int)mSetup.nodes.size());
        ImGui::Text("Cameras     : %d", (int)mSetup.cameras.size());
    }

    ImGui::SeparatorText("Boundaries");
    ImGui::Text("Min         : %d, %d, %d", mSetup.boundsMin[0] * 1000, mSetup.boundsMin[1] * 1000,
                mSetup.boundsMin[2] * 1000);
    ImGui::Text("Max         : %d, %d, %d", (mSetup.boundsMax[0] + 1) * 1000, (mSetup.boundsMax[1] + 1) * 1000,
                (mSetup.boundsMax[2] + 1) * 1000);
    ImGui::TextDisabled("cube grid %d..%d / %d..%d / %d..%d", mSetup.boundsMin[0], mSetup.boundsMax[0],
                        mSetup.boundsMin[1], mSetup.boundsMax[1], mSetup.boundsMin[2], mSetup.boundsMax[2]);
    ImGui::Spacing();
    Lightbulb::ui::TextDisabledWrapped("Boundary editing and level model replacement arrive with o2r writing.");
}

void App::DrawStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float barHeight = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - barHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, barHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoSavedSettings;
    float padY = (barHeight - ImGui::GetTextLineHeight()) * 0.5f;
    if (padY < 0.0f) {
        padY = 0.0f;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, padY));
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        ImGui::TextUnformatted(mStatus.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
