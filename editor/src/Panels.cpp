#include "App.h"
#include "LevelView.h"
#include "O2rImport.h"

#include "PreviewScene.h"
#include "SetupNames.h"
#include "UiCommon.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
using Lightbulb::indexById;
using Lightbulb::kDeg;
using Lightbulb::kLevelFovYDeg;

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

const int kCamFilterCount = 6;
const char* camFilterLabel(int selected) {
    return selected <= 0 ? "All types" : Lightbulb::CameraTypeName((uint8_t)(selected - 1));
}

bool cameraMatches(int selected, const char* filter, const Lightbulb::SetupCamera& cam) {
    if (selected > 0 && (int)cam.type != selected - 1) {
        return false;
    }
    if (filter[0] == '\0') {
        return true;
    }
    char index[16];
    std::snprintf(index, sizeof(index), "%d", (int)cam.index);
    return Lightbulb::ui::ContainsNoCase(Lightbulb::CameraTypeName(cam.type), filter) ||
           Lightbulb::ui::ContainsNoCase(index, filter);
}

void lookVectors(float yawDeg, float pitchDeg, float outForward[3], float outRight[3]) {
    const float cosPitch = std::cos(pitchDeg * kDeg);
    outForward[0] = cosPitch * std::sin(yawDeg * kDeg);
    outForward[1] = std::sin(pitchDeg * kDeg);
    outForward[2] = -cosPitch * std::cos(yawDeg * kDeg);
    outRight[0] = std::cos(yawDeg * kDeg);
    outRight[1] = 0.0f;
    outRight[2] = std::sin(yawDeg * kDeg);
}

bool isNamedCategory(uint8_t category) {
    return category == 3 || category == 4 || category == 6 || category == 7 || category == 8 || category == 9 ||
           category == 10;
}

// Props are told apart by type, nodes by category
struct ObjectKind {
    const char* label;
    bool node;
    int value;
};
const ObjectKind kObjectKinds[] = {
    { "All kinds", false, -1 },    { "Model", false, 2 },          { "Sprite", false, 0 },
    { "Warp", true, 3 },           { "Contact trigger", true, 4 }, { "Actor spawn", true, 6 },
    { "Enemy boundary", true, 7 }, { "Path node", true, 8 },       { "Camera trigger", true, 9 },
    { "Flag", true, 10 },          { "Other nodes", true, -1 },    { "Script waypoint", true, -2 },
};
const int kObjectKindCount = (int)(sizeof(kObjectKinds) / sizeof(kObjectKinds[0]));

bool kindMatchesProp(int selected, uint8_t type) {
    if (selected <= 0) {
        return true;
    }
    const ObjectKind& kind = kObjectKinds[selected];
    return !kind.node && kind.value == (int)type;
}

bool kindMatchesNode(int selected, const Lightbulb::SetupNode& node) {
    if (selected <= 0) {
        return true;
    }
    const ObjectKind& kind = kObjectKinds[selected];
    if (!kind.node) {
        return false;
    }
    if (kind.value == -2) {
        return node.script != 0;
    }
    if (node.script) {
        return false;
    }
    return kind.value >= 0 ? kind.value == (int)node.category : !isNamedCategory(node.category);
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

void App::EnsureAssetIndexes() {
    if (mModelIndex.empty()) {
        mModelIndex = indexById(Lightbulb::ListO2rModelPaths("assets/model"));
        mSpriteIndex = indexById(Lightbulb::ListO2rSpritePaths());
    }
}

void App::FrameEyeAtEntry(const Lightbulb::SetupNode& node) {
    mLevelScene.eye[0] = (float)node.pos[0];
    mLevelScene.eye[1] = (float)node.pos[1] + 50.0f;
    mLevelScene.eye[2] = (float)node.pos[2] + 500.0f;
    mLevelScene.yaw = 0.0f;
    mLevelScene.pitch = 0.0f;
}

bool App::SelectionFocusTarget(float outCenter[3], float& outRadius) const {
    if (mPropSel < 0) {
        return false;
    }

    // Prefer what the renderer actually drew
    for (const PickTarget& target : mPickTargets) {
        if (target.sel != mPropSel) {
            continue;
        }
        float diagonal = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            outCenter[axis] = (target.min[axis] + target.max[axis]) * 0.5f;
            const float extent = target.max[axis] - target.min[axis];
            diagonal += extent * extent;
        }
        outRadius = std::sqrt(diagonal) * 0.5f;
        return true;
    }

    // Not drawn this frame (hidden layer, culled, or past the pool limit); fall back to the setup position.
    const int propCount = (int)mSetup.props.size();
    const int nodeCount = (int)mSetup.nodes.size();
    if (mPropSel < propCount) {
        const Lightbulb::SetupProp& prop = mSetup.props[mPropSel];
        for (int axis = 0; axis < 3; ++axis) {
            outCenter[axis] = (float)prop.pos[axis];
        }
    } else if (mPropSel < propCount + nodeCount) {
        const Lightbulb::SetupNode& node = mSetup.nodes[mPropSel - propCount];
        for (int axis = 0; axis < 3; ++axis) {
            outCenter[axis] = (float)node.pos[axis];
        }
    } else if (mPropSel < propCount + nodeCount + (int)mSetup.cameras.size()) {
        const Lightbulb::SetupCamera& camera = mSetup.cameras[mPropSel - propCount - nodeCount];
        for (int axis = 0; axis < 3; ++axis) {
            outCenter[axis] = camera.pos[axis];
        }
    } else {
        return false;
    }
    outRadius = 0.0f;
    return true;
}

void App::FocusSelection() {
    float center[3];
    float radius = 0.0f;
    if (!SelectionFocusTarget(center, radius)) {
        return;
    }
    // Dolly along the current heading; spinning the camera to face the object is disorienting.
    float forward[3], right[3];
    lookVectors(mLevelScene.yaw, mLevelScene.pitch, forward, right);
    float distance = radius / std::tan(kLevelFovYDeg * 0.5f * kDeg) * 1.6f;
    if (distance < 150.0f) {
        distance = 150.0f;
    }
    for (int axis = 0; axis < 3; ++axis) {
        mLevelScene.eye[axis] = center[axis] - forward[axis] * distance;
    }
    mLevelScene.framed = true;
}

void App::EnsureLevelEntries() {
    LevelScene& scene = mLevelScene;
    if (!scene.entries.empty()) {
        return;
    }
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

void App::SelectLevel(int row) {
    LevelScene& scene = mLevelScene;
    scene.sel = row;
    scene.framed = false;
    mPropSel = -1;
    ResumeLevelMusic();
    if (!scene.entries[row].setupPath.empty()) {
        Lightbulb::LoadO2rSetup(scene.entries[row].setupPath, mSetup);
    } else {
        mSetup = Lightbulb::SetupScene{};
    }
    ResetHistory();
    mConfig.lastMapId = scene.entries[row].mapId;
    SaveSettings();
}

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
    EnsureLevelEntries();

    const float availY = ImGui::GetContentRegionAvail().y;
    if (ImGui::CollapsingHeader("Levels", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##levelfilter", "search levels", mLevelFilter, sizeof(mLevelFilter));
        ImGui::BeginChild("##levellist", ImVec2(0, availY * 0.4f), true);
        for (int row = 0; row < (int)scene.entries.size(); ++row) {
            if (mLevelFilter[0] != '\0') {
                char mapHex[8];
                std::snprintf(mapHex, sizeof(mapHex), "%X", (unsigned)scene.entries[row].mapId);
                if (!Lightbulb::ui::ContainsNoCase(scene.entries[row].name.c_str(), mLevelFilter) &&
                    !Lightbulb::ui::ContainsNoCase(mapHex, mLevelFilter)) {
                    continue;
                }
            }
            char label[208];
            std::snprintf(label, sizeof(label), "%s##lvl%d", scene.entries[row].name.c_str(), row);
            if (ImGui::Selectable(label, row == scene.sel)) {
                SelectLevel(row);
            }
        }
        ImGui::EndChild();
    }

    if (mSetup.loaded && ImGui::CollapsingHeader("Entry points")) {
        const int propCount = (int)mSetup.props.size();
        std::vector<int> entryIdx;
        for (int idx = 0; idx < (int)mSetup.nodes.size(); ++idx) {
            const Lightbulb::SetupNode& nd = mSetup.nodes[idx];
            if (!nd.script && nd.category == 6 && Lightbulb::EditorEntryPointId(nd.id)) {
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
                FrameEyeAtEntry(nd);
            }
        }
        if (entryIdx.empty()) {
            Lightbulb::ui::TextDisabledWrapped("No entry points in this setup.");
        }
        ImGui::EndChild();
    }

    if (ImGui::BeginTabBar("##lefttabs")) {
        if (ImGui::BeginTabItem("Objects", nullptr, mRevealTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            DrawObjectsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Cameras", nullptr, mRevealTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            DrawCamerasTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Paths")) {
            DrawPathsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    // The pick below runs after the lists, so a reveal lands next frame; clear it once they've seen it.
    mScrollToSel = false;
    mRevealTab = -1;

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
            float forward[3], right[3];
            lookVectors(scene.yaw, scene.pitch, forward, right);
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

            if (best < 0) {
                const float allowed = 32.0f / viewH;
                float bestOffset = 0.0f;
                for (const PickTarget& target : mPickTargets) {
                    float offset = 1e30f;
                    for (int corner = 0; corner < 8; ++corner) {
                        float toPoint[3];
                        float along = 0.0f;
                        for (int axis = 0; axis < 3; ++axis) {
                            const float p = (corner & (1 << axis)) ? target.max[axis] : target.min[axis];
                            toPoint[axis] = p - scene.eye[axis];
                            along += toPoint[axis] * dir[axis];
                        }
                        if (along <= 1.0f) {
                            continue;
                        }
                        float perpSq = 0.0f;
                        for (int axis = 0; axis < 3; ++axis) {
                            const float perp = toPoint[axis] - dir[axis] * along;
                            perpSq += perp * perp;
                        }
                        offset = std::min(offset, std::sqrt(perpSq) / (along * tanHalf));
                    }
                    if (offset > allowed) {
                        continue;
                    }
                    if (best < 0 || offset < bestOffset) {
                        bestOffset = offset;
                        best = target.sel;
                    }
                }
            }
            if (best >= 0) {
                mScrollToSel = true;
                mRevealTab = best >= (int)mSetup.props.size() + (int)mSetup.nodes.size() ? 1 : 0;
            }
            mPropSel = best;
        }
        if (scene.looking && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            scene.yaw += io.MouseDelta.x * 0.25f;
            scene.pitch -= io.MouseDelta.y * 0.25f;
            scene.pitch = scene.pitch > 89.0f ? 89.0f : (scene.pitch < -89.0f ? -89.0f : scene.pitch);
        }
        if (overView && !io.WantTextInput) {
            if (io.MouseWheel != 0.0f) {
                mConfig.cameraSpeed =
                    std::clamp(mConfig.cameraSpeed + (io.MouseWheel > 0.0f ? 10.0f : -10.0f), 10.0f, 100.0f);
                SaveSettings();
            }
            float look[3], right[3];
            lookVectors(scene.yaw, scene.pitch, look, right);
            float speed = mConfig.cameraSpeed;
            if (io.KeyShift) {
                speed += 20.0f;
            }
            if (io.KeyCtrl) {
                speed -= 20.0f;
            }
            const float step = std::max(speed, 5.0f) * io.DeltaTime * 60.0f;
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
            if (ImGui::IsKeyPressed(ImGuiKey_F))
                FocusSelection();
            EditShortcuts();
        }
    }

    ImGui::End();
}

void App::DrawObjectsTab() {
    if (!mSetup.loaded) {
        ImGui::TextWrapped("Select a level to load its objects.");
        return;
    }

    EnsureAssetIndexes();

    const int propCount = (int)mSetup.props.size();
    const int nodeCount = (int)mSetup.nodes.size();

    auto propLabel = [&](const Lightbulb::SetupProp& prop, bool& dim) {
        dim = false;
        if (prop.type == 2 || prop.type == 0) {
            const std::map<uint32_t, std::string>& index = prop.type == 2 ? mModelIndex : mSpriteIndex;
            const uint32_t assetId = (prop.type == 2 ? 0x2D1u : 0x572u) + prop.id;
            const auto found = index.find(assetId);
            if (found != index.end()) {
                return assetShortName(found->second);
            }
            dim = true;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s %X (missing)", prop.type == 2 ? "model" : "sprite", assetId);
            return std::string(buf);
        }
        if (const char* actorName = Lightbulb::ActorEnumName(prop.id)) {
            return std::string(actorName);
        }
        dim = true;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "actor %X", prop.id);
        return std::string(buf);
    };

    auto nodeLabel = [&](const Lightbulb::SetupNode& node, bool& dim) {
        if (node.script) {
            dim = false;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "uid %u at %.3f", (unsigned)node.pathUid, node.legFraction);
            return std::string(buf);
        }
        if (node.category == 6) {
            if (const char* actorName = Lightbulb::ActorEnumName(node.id)) {
                dim = false;
                return std::string(actorName);
            }
        }
        dim = true;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "id %u (0x%X)", node.id, node.id);
        return std::string(buf);
    };

    ImGui::SetNextItemWidth(-160.0f);
    ImGui::InputTextWithHint("##objfilter", "search", mObjFilter, sizeof(mObjFilter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##objkind", kObjectKinds[mObjKind].label)) {
        for (int kind = 0; kind < kObjectKindCount; ++kind) {
            if (ImGui::Selectable(kObjectKinds[kind].label, kind == mObjKind)) {
                mObjKind = kind;
            }
        }
        ImGui::EndCombo();
    }

    const bool searching = mObjFilter[0] != '\0';
    bool dim = false;
    mObjVisible.clear();
    for (int row = 0; row < propCount; ++row) {
        const Lightbulb::SetupProp& prop = mSetup.props[row];
        if (!kindMatchesProp(mObjKind, prop.type)) {
            continue;
        }
        if (searching && !Lightbulb::ui::ContainsNoCase(Lightbulb::PropKindName(prop.type), mObjFilter) &&
            !Lightbulb::ui::ContainsNoCase(propLabel(prop, dim).c_str(), mObjFilter)) {
            continue;
        }
        mObjVisible.push_back(row);
    }
    for (int nodeRow = 0; nodeRow < nodeCount; ++nodeRow) {
        const Lightbulb::SetupNode& node = mSetup.nodes[nodeRow];
        if (!kindMatchesNode(mObjKind, node)) {
            continue;
        }
        if (searching && !Lightbulb::ui::ContainsNoCase(Lightbulb::NodeKindName(node), mObjFilter) &&
            !Lightbulb::ui::ContainsNoCase(nodeLabel(node, dim).c_str(), mObjFilter)) {
            continue;
        }
        mObjVisible.push_back(propCount + nodeRow);
    }

    const int total = propCount + nodeCount;
    if ((int)mObjVisible.size() == total) {
        ImGui::Text("cubes %d   objects %d   spawns/warps/triggers %d   cameras %d", mSetup.cubeCount, propCount,
                    nodeCount, (int)mSetup.cameras.size());
    } else {
        ImGui::Text("showing %d of %d   cubes %d   cameras %d", (int)mObjVisible.size(), total, mSetup.cubeCount,
                    (int)mSetup.cameras.size());
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##o2robjs")) {
        if (ImGui::BeginTable("##props", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            // Only rows in view get built; the one being revealed is forced in so it can be scrolled to.
            ImGuiListClipper clipper;
            clipper.Begin((int)mObjVisible.size());
            if (mScrollToSel) {
                const auto revealed = std::find(mObjVisible.begin(), mObjVisible.end(), mPropSel);
                if (revealed != mObjVisible.end()) {
                    clipper.IncludeItemByIndex((int)(revealed - mObjVisible.begin()));
                }
            }
            while (clipper.Step()) {
                for (int item = clipper.DisplayStart; item < clipper.DisplayEnd; ++item) {
                    const int row = mObjVisible[item];
                    const bool isNode = row >= propCount;
                    const std::string label =
                        isNode ? nodeLabel(mSetup.nodes[row - propCount], dim) : propLabel(mSetup.props[row], dim);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    char sel[32];
                    if (isNode) {
                        std::snprintf(sel, sizeof(sel), "%d##node%d", row, row - propCount);
                    } else {
                        std::snprintf(sel, sizeof(sel), "%d##prop%d", row, row);
                    }
                    if (ImGui::Selectable(sel, row == mPropSel,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowDoubleClick)) {
                        mPropSel = row;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            FocusSelection();
                        }
                    }
                    if (mScrollToSel && row == mPropSel) {
                        ImGui::SetScrollHereY(0.5f);
                    }
                    ImGui::TableNextColumn();
                    if (isNode) {
                        ImGui::TextDisabled("%s", Lightbulb::NodeKindName(mSetup.nodes[row - propCount]));
                    } else {
                        ImGui::TextDisabled("%s", Lightbulb::PropKindName(mSetup.props[row].type));
                    }
                    ImGui::TableNextColumn();
                    if (dim) {
                        ImGui::TextDisabled("%s", label.c_str());
                    } else {
                        ImGui::TextUnformatted(label.c_str());
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        EditShortcuts();
    }
}

void App::DrawCamerasTab() {
    if (!mSetup.loaded) {
        ImGui::TextWrapped("Select a level to see its cameras.");
        return;
    }

    ImGui::SetNextItemWidth(-160.0f);
    ImGui::InputTextWithHint("##camfilter", "search", mCamFilter, sizeof(mCamFilter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##camtype", camFilterLabel(mCamType))) {
        for (int kind = 0; kind < kCamFilterCount; ++kind) {
            if (ImGui::Selectable(camFilterLabel(kind), kind == mCamType)) {
                mCamType = kind;
            }
        }
        ImGui::EndCombo();
    }

    int shown = 0;
    for (const Lightbulb::SetupCamera& c : mSetup.cameras) {
        shown += cameraMatches(mCamType, mCamFilter, c) ? 1 : 0;
    }
    if (shown == (int)mSetup.cameras.size()) {
        ImGui::Text("%d cameras", shown);
    } else {
        ImGui::Text("showing %d of %d cameras", shown, (int)mSetup.cameras.size());
    }
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
                if (!cameraMatches(mCamType, mCamFilter, c)) {
                    continue;
                }
                const int row = camBase + (int)(&c - mSetup.cameras.data());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char sel[32];
                std::snprintf(sel, sizeof(sel), "%d##cam%d", (int)c.index, row);
                if (ImGui::Selectable(sel, row == mPropSel,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    mPropSel = row;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        FocusSelection();
                    }
                }
                if (mScrollToSel && row == mPropSel) {
                    ImGui::SetScrollHereY(0.5f);
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s (%u)", Lightbulb::CameraTypeName(c.type), (unsigned)c.type);
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
        if (!nd.script && nd.category == 8) {
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
                if (ImGui::Selectable(sel, row == mPropSel,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    mPropSel = row;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        FocusSelection();
                    }
                }
                if (mScrollToSel && row == mPropSel) {
                    ImGui::SetScrollHereY(0.5f);
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
            ImGui::SetTooltip(
                "Show actor spawns this map's spawn queues never register. Vanilla only, romhacks unaffected.");
        }
    }
    if (changed) {
        SaveSettings();
    }
    ImGui::End();
}
