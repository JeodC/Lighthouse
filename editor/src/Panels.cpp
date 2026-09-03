#include "App.h"
#include "LevelView.h"
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
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

const char* camTypeName(uint8_t type) {
    static const char* kNames[] = { "?", "Pivot", "Static", "Zoom", "Random" };
    return type < 5 ? kNames[type] : "?";
}

const int kCamFilterCount = 6;
const char* camFilterLabel(int selected) {
    return selected <= 0 ? "All types" : camTypeName((uint8_t)(selected - 1));
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
    return Lightbulb::ui::ContainsNoCase(camTypeName(cam.type), filter) ||
           Lightbulb::ui::ContainsNoCase(index, filter);
}

const char* nodeCategoryName(uint8_t category) {
    switch (category) {
        case 3:
            return "Warp";
        case 4:
            return "Contact trigger";
        case 6:
            return "Actor spawn";
        case 7:
            return "Enemy boundary";
        case 8:
            return "Path node";
        case 9:
            return "Camera trigger";
        case 10:
            return "Flag";
        default:
            return "Node";
    }
}

const char* nodeCategoryDescription(uint8_t category) {
    switch (category) {
        case 3:
            return "Touching this box warps Banjo. Its id picks the warp handler, which carries the destination map "
                   "and entrance with it.";
        case 4:
            return "Touching this box fires a scripted trigger chosen by its id.";
        case 6:
            return "Spawns this actor where the node sits, facing its yaw. Radius and scale are free parameters the "
                   "actor reads however it likes.";
        case 7:
            return "Enemies belonging to this zone won't move outside it. Overlapping boundaries with the same id "
                   "form one zone.";
        case 8:
            return "A path control point. Its link ids chain it to the next node, and actors spawned on the chain "
                   "follow the path they form.";
        case 10:
            return "Collectibles standing in this cylinder read its id to learn which save flag they set - a jiggy "
                   "uses the id plus one, so moving one out of its volume changes what it records.";
        default:
            return nullptr;
    }
}

const char* contactTriggerDetail(uint16_t id) {
    if (id >= 0x16 && id <= 0x29) {
        return "This id starts the area camera for one of the map's camera areas.";
    }
    if (id == 0x2A) {
        return "This id ends the area camera, and is the most placed trigger in the game.";
    }
    if (id >= 0x46 && id <= 0x4B) {
        return "This id is a treasure hunt step.";
    }
    if (id == 0x4C || id == 0x4D) {
        return "This id is the Mumbo transformation boundary.";
    }
    return nullptr;
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

const char* propKindName(uint8_t type) {
    return type == 2 ? "Model" : type == 0 ? "Sprite" : "Actor";
}

// Props are told apart by type, nodes by category
struct ObjectKind {
    const char* label;
    bool node;
    int value;
};
const ObjectKind kObjectKinds[] = {
    { "All kinds", false, -1 },       { "Model", false, 2 },
    { "Sprite", false, 0 },
    { "Warp", true, 3 },              { "Contact trigger", true, 4 },
    { "Actor spawn", true, 6 },       { "Enemy boundary", true, 7 },
    { "Path node", true, 8 },         { "Camera trigger", true, 9 },
    { "Flag", true, 10 },             { "Other nodes", true, -1 },
    { "Script waypoint", true, -2 },
};
const int kObjectKindCount = (int)(sizeof(kObjectKinds) / sizeof(kObjectKinds[0]));

bool kindMatchesProp(int selected, uint8_t type) {
    if (selected <= 0) {
        return true;
    }
    const ObjectKind& kind = kObjectKinds[selected];
    return !kind.node && kind.value == (int)type;
}

// Names for the two 3-bit switches in func_803422D4, the routine that applies a waypoint.
const char* legHeadingModeName(uint8_t mode) {
    static const char* kNames[] = {
        "unchanged",       "face along the path",   "stop using yaw",        "use yaw",
        "stop using pitch", "use pitch",            "stop using yaw+pitch",  "use yaw+pitch",
    };
    return mode < 8 ? kNames[mode] : "?";
}

const char* legAnimModeName(uint8_t mode) {
    static const char* kNames[] = { "unchanged", "unchanged", "play once", "play once reversed",
                                    "loop",      "loop reversed", "hold",  "unchanged" };
    return mode < 8 ? kNames[mode] : "?";
}

const char* nodeKindName(const Lightbulb::SetupNode& node) {
    return node.script ? "Script waypoint" : nodeCategoryName(node.category);
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
                mConfig.cameraSpeed = std::clamp(mConfig.cameraSpeed + (io.MouseWheel > 0.0f ? 10.0f : -10.0f),
                                                 10.0f, 100.0f);
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
        if (searching && !Lightbulb::ui::ContainsNoCase(propKindName(prop.type), mObjFilter) &&
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
        if (searching && !Lightbulb::ui::ContainsNoCase(nodeKindName(node), mObjFilter) &&
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
            for (const int row : mObjVisible) {
                const bool isNode = row >= propCount;
                const std::string label = isNode ? nodeLabel(mSetup.nodes[row - propCount], dim)
                                                 : propLabel(mSetup.props[row], dim);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char sel[32];
                if (isNode) {
                    std::snprintf(sel, sizeof(sel), "%d##node%d", row, row - propCount);
                } else {
                    std::snprintf(sel, sizeof(sel), "%d##prop%d", row, row);
                }
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
                if (isNode) {
                    ImGui::TextDisabled("%s", nodeKindName(mSetup.nodes[row - propCount]));
                } else {
                    ImGui::TextDisabled("%s", propKindName(mSetup.props[row].type));
                }
                ImGui::TableNextColumn();
                if (dim) {
                    ImGui::TextDisabled("%s", label.c_str());
                } else {
                    ImGui::TextUnformatted(label.c_str());
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

void App::EditShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        Undo();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        Redo();
    } else if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        DeleteSelection();
    }
}

void App::ResetHistory() {
    mHistory.clear();
    mHistoryPos = -1;
    if (mSetup.loaded) {
        mHistory.push_back({ mSetup.props, mSetup.nodes, "Level loaded" });
        mHistoryPos = 0;
    }
}

// Called after the change, so every step in the list is a state the level was in.
void App::RecordEdit(const std::string& label) {
    if (mHistoryPos < 0) {
        ResetHistory();
        return;
    }
    mHistory.resize(mHistoryPos + 1);
    mHistory.push_back({ mSetup.props, mSetup.nodes, label });
    if (mHistory.size() > 64) {
        mHistory.erase(mHistory.begin());
    }
    mHistoryPos = (int)mHistory.size() - 1;
}

void App::ApplyHistory(int step) {
    if (step < 0 || step >= (int)mHistory.size()) {
        return;
    }
    mSetup.props = mHistory[step].props;
    mSetup.nodes = mHistory[step].nodes;
    mHistoryPos = step;
    mPropSel = -1;
    mScrollToSel = false;
    mStatus = "History: " + mHistory[step].label;
}

void App::Undo() {
    if (mHistoryPos <= 0) {
        mStatus = "Nothing to undo.";
        return;
    }
    ApplyHistory(mHistoryPos - 1);
}

void App::Redo() {
    if (mHistoryPos < 0 || mHistoryPos + 1 >= (int)mHistory.size()) {
        mStatus = "Nothing to redo.";
        return;
    }
    ApplyHistory(mHistoryPos + 1);
}

// Drops the record from the loaded scene only - the archive is never written.
void App::DeleteSelection() {
    const int propCount = (int)mSetup.props.size();
    if (mPropSel < 0 || mPropSel >= propCount + (int)mSetup.nodes.size()) {
        return;
    }
    char what[64];
    if (mPropSel < propCount) {
        std::snprintf(what, sizeof(what), "%s #%d", propKindName(mSetup.props[mPropSel].type), mPropSel);
        mSetup.props.erase(mSetup.props.begin() + mPropSel);
    } else {
        const int row = mPropSel - propCount;
        std::snprintf(what, sizeof(what), "%s #%d", nodeKindName(mSetup.nodes[row]), row);
        mSetup.nodes.erase(mSetup.nodes.begin() + row);
    }
    mPropSel = -1;
    mScrollToSel = false;
    RecordEdit(std::string("Removed ") + what);
    mStatus = std::string("Removed ") + what + " from the loaded level. Ctrl-Z puts it back.";
}

void App::DrawHistory() {
    if (!mShowHistory) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_Appearing);
    if (!ImGui::Begin("History", &mShowHistory)) {
        ImGui::End();
        return;
    }
    if (mHistory.empty()) {
        ImGui::TextWrapped("Select a level to start a history.");
        ImGui::End();
        return;
    }
    Lightbulb::ui::TextDisabledWrapped("Steps of the loaded level. Pick one to put the level back in that state.");
    ImGui::Separator();
    for (int step = 0; step < (int)mHistory.size(); ++step) {
        char label[96];
        std::snprintf(label, sizeof(label), "%d. %s##step%d", step, mHistory[step].label.c_str(), step);
        if (ImGui::Selectable(label, step == mHistoryPos)) {
            ApplyHistory(step);
        }
    }
    ImGui::End();
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
                ImGui::TextDisabled("%s (%u)", camTypeName(c.type), (unsigned)c.type);
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
    EnsureAssetIndexes();
    if (mLevelScene.sel >= 0) {
        // Yaw the way a node stores it, 0-359, so it compares against Yaw (raw) below.
        float yaw = std::fmod(mLevelScene.yaw, 360.0f);
        if (yaw < 0.0f) {
            yaw += 360.0f;
        }
        ImGui::Text("Camera: posx %.0f, posy %.0f, posz %.0f", mLevelScene.eye[0], mLevelScene.eye[1],
                    mLevelScene.eye[2]);
        ImGui::Text("        yaw %.0f | pitch %.0f", yaw, mLevelScene.pitch);
        ImGui::Separator();
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
        static const char* kCamPurpose[] = {
            nullptr,
            "Stays where it is and turns to keep Banjo in view.",
            "Fixed view, position and angle both authored. Only cutscenes and scripts cut to it; "
            "walking around never selects it.",
            "Follows Banjo along the line toward him, staying between its close and far distance.",
            "Picks a fallback camera behavior instead of framing a view itself; its position fields are unused.",
        };
        ImGui::Text("Camera #%d", (int)cam.index);
        ImGui::Separator();
        ImGui::Text("Type        : %s (%u)", camTypeName(cam.type), (unsigned)cam.type);
        if (cam.type < 5 && kCamPurpose[cam.type]) {
            Lightbulb::ui::TextDisabledWrapped(kCamPurpose[cam.type]);
        }
        int volumes = 0;
        for (const Lightbulb::SetupNode& nd : mSetup.nodes) {
            if (!nd.script && nd.category == 9 && nd.id == cam.index) {
                ++volumes;
            }
        }
        if (volumes > 0) {
            Lightbulb::ui::TextDisabledWrapped(
                "This camera takes over while Banjo stands inside any of its %d trigger box%s that are drawn in "
                "the same color as this camera.",
                volumes, volumes == 1 ? "" : "es");
        } else if (cam.type != 2) {
            Lightbulb::ui::TextDisabledWrapped(
                "No trigger boxes point at this camera; only game code can switch to it.");
        }
        ImGui::Text("Position    : %.0f, %.0f, %.0f", cam.pos[0], cam.pos[1], cam.pos[2]);
        ImGui::Text("Pitch       : %.1f", cam.pitchYawRoll[0]);
        ImGui::Text("Yaw         : %.1f", cam.pitchYawRoll[1]);
        ImGui::Text("Roll        : %.1f", cam.pitchYawRoll[2]);
        return;
    }

    if (mPropSel >= propCount) {
        const Lightbulb::SetupNode& node = mSetup.nodes[mPropSel - propCount];
        if (node.script) {
            ImGui::Text("Node #%d", mPropSel);
            ImGui::Separator();
            ImGui::Text("Category    : Script waypoint");
            Lightbulb::ui::TextDisabledWrapped("One leg of a scripted path, chained with its control points. "
                                               "The rider applies these once it passes the fraction.");
            ImGui::Text("Chain       : uid %u -> %s", (unsigned)node.pathUid,
                        node.pathNext ? std::to_string(node.pathNext).c_str() : "end");
            ImGui::Text("At fraction : %.3f", node.legFraction);

            auto applied = [](const char* label, bool on, const char* fmt, ...) {
                va_list args;
                va_start(args, fmt);
                char value[96];
                std::vsnprintf(value, sizeof(value), fmt, args);
                va_end(args);
                if (on) {
                    ImGui::Text("%s: %s", label, value);
                } else {
                    ImGui::TextDisabled("%s: %s (not applied)", label, value);
                }
            };
            // The file keeps times in quarter seconds; show seconds, the unit the doc uses.
            applied("Speed       ", (node.legApply & 2) != 0, "%u", (unsigned)node.legSpeed);
            if (node.legPauseIsAlt) {
                applied("Pause       ", (node.legApply & 1) != 0, "%u (special)", (unsigned)node.legPause);
            } else {
                applied("Pause       ", (node.legApply & 1) != 0, "%.2f s", node.legPause / 4.0f);
            }
            applied("Animation   ", (node.legApply & 4) != 0, "#%u for %.2f s, %s", (unsigned)node.legAnim,
                    node.legAnimDuration / 4.0f, legAnimModeName(node.legAnimMode));

            ImGui::Text("Heading     : %s", legHeadingModeName(node.legHeadingMode));
            if (node.legHeadingMode != 1) {
                ImGui::Text("Yaw / pitch : %u / %u", (unsigned)node.legYaw, (unsigned)node.legPitch);
            }
            if (node.legLinkUid) {
                ImGui::Text("Blend to    : uid %u (%s%s)", (unsigned)node.legLinkUid,
                            (node.legBlend & 1) ? "heading" : "", (node.legBlend & 2) ? " speed" : "");
            }
            if (node.legSmoothTurn) {
                ImGui::Text("Smooth turn : yes");
            }
            return;
        }
        const bool isSpawn = node.category == 6;
        const char* actorName = isSpawn ? Lightbulb::ActorEnumName(node.id) : nullptr;
        ImGui::Text("Node #%d", mPropSel);
        ImGui::Separator();
        ImGui::Text("Category    : %s (%u)", nodeCategoryName(node.category), (unsigned)node.category);
        if (node.category == 9) {
            const Lightbulb::SetupCamera* target = nullptr;
            for (const Lightbulb::SetupCamera& sc : mSetup.cameras) {
                if (sc.index == (int)node.id) {
                    target = &sc;
                    break;
                }
            }
            if (target) {
                Lightbulb::ui::TextDisabledWrapped(
                    "While Banjo stands in this box, Camera #%u (%s) takes over. It and this box share a color in "
                    "the viewport.",
                    node.id, camTypeName(target->type));
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                                   "Points at Camera #%u, which doesn't exist - free camera here", node.id);
            }
        } else if (const char* description = nodeCategoryDescription(node.category)) {
            Lightbulb::ui::TextDisabledWrapped(description);
            if (node.category == 4) {
                if (const char* detail = contactTriggerDetail(node.id)) {
                    Lightbulb::ui::TextDisabledWrapped(detail);
                }
            }
        }
        if (isSpawn) {
            if (actorName) {
                ImGui::Text("Actor       : %s", actorName);
            } else {
                ImGui::Text("Actor       : 0x%X (not in the actor enum)", node.id);
            }
        }
        ImGui::Text("Id          : %u (0x%X)", node.id, node.id);
        if (isSpawn && !Lightbulb::EditorEntryPointId(node.id) && Lightbulb::ActorIsSpawnable(node.id) &&
            mRomhackPath.empty() && mLevelScene.sel >= 0 && mLevelScene.sel < (int)mLevelScene.entries.size() &&
            !Lightbulb::ActorRegisteredForMap(mLevelScene.entries[mLevelScene.sel].mapId, node.id)) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f), "Never spawns  : map doesn't register this actor");
        }
        if (isSpawn) {
            const uint32_t modelAsset = Lightbulb::ActorModelAsset(node.id);
            if (modelAsset == 0) {
                if (Lightbulb::ActorHasModelInfo(node.id)) {
                    ImGui::Text("Model asset : none");
                } else {
                    ImGui::Text("Model asset : unknown");
                }
            } else if (mModelIndex.count(modelAsset)) {
                ImGui::Text("Model asset : %s", assetFullName(mModelIndex, modelAsset).c_str());
            } else if (mSpriteIndex.count(modelAsset)) {
                ImGui::Text("Sprite asset: %s", assetFullName(mSpriteIndex, modelAsset).c_str());
            } else {
                ImGui::Text("Asset       : ASSET_%X (missing)", modelAsset);
            }
        }
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

namespace {
struct HudIcon {
    const char* name;
    const char* texture;
    const char* palette;
};
// The sprites Lighthouse's world tracker loads, in the order the counts are stacked.
const HudIcon kHudIcons[9] = {
    { "Jiggy", "assets/sprite/ASSET_80D_LIVE_JIGGY_1_0", "assets/sprite/ASSET_80D_LIVE_JIGGY_1_TLUT" },
    { "Mumbo Token", "assets/sprite/ASSET_41A_MUMBO_TOKEN_1_0", "assets/sprite/ASSET_41A_MUMBO_TOKEN_1_TLUT" },
    { "Empty Honeycomb", "assets/sprite/ASSET_81D_LIVE_EXTRA_HEALTH_MAX_1_0",
      "assets/sprite/ASSET_81D_LIVE_EXTRA_HEALTH_MAX_1_TLUT" },
    { "Music Note", "assets/sprite/ASSET_81B_LIVE_MUSIC_NOTE_1_0", "assets/sprite/ASSET_81B_LIVE_MUSIC_NOTE_1_TLUT" },
    { "Blue Jinjo", "assets/sprite/ASSET_804_JINJO_BLUE_0_0", "assets/sprite/ASSET_804_JINJO_BLUE_0_TLUT" },
    { "Green Jinjo", "assets/sprite/ASSET_803_JINJO_GREEN_0_0", "assets/sprite/ASSET_803_JINJO_GREEN_0_TLUT" },
    { "Orange Jinjo", "assets/sprite/ASSET_806_JINJO_ORANGE_0_0", "assets/sprite/ASSET_806_JINJO_ORANGE_0_TLUT" },
    { "Pink Jinjo", "assets/sprite/ASSET_805_JINJO_PINK_0_0", "assets/sprite/ASSET_805_JINJO_PINK_0_TLUT" },
    { "Yellow Jinjo", "assets/sprite/ASSET_802_JINJO_YELLOW_0_0", "assets/sprite/ASSET_802_JINJO_YELLOW_0_TLUT" },
};
constexpr uint32_t kJinjoActors[5] = { 0x60, 0x62, 0x5F, 0x61, 0x5E };
constexpr float kHudIconPx = 32.0f;
constexpr float kHudJinjoPx = 48.0f;
constexpr float kHudGapPx = 6.0f;
constexpr float kHudEdgeXPx = 10.0f;
constexpr float kHudEdgeYPx = 20.0f;
} // namespace

void App::DrawLevelHud() {
    if (!mSetup.loaded) {
        return;
    }
    float vx, vy, vw, vh;
    if (!Lightbulb::GetGameViewportRect(vx, vy, vw, vh)) {
        return;
    }

    if (!mHudTexReady) {
        for (const HudIcon& icon : kHudIcons) {
            Lightbulb::LoadO2rGuiTexture(icon.name, icon.texture, icon.palette);
        }
        mHudTexReady = true;
    }

    int counts[4] = { 0, 0, 0, 0 }; // jiggies, mumbo tokens, empty honeycombs, music notes
    bool jinjos[5] = { false, false, false, false, false };
    for (const Lightbulb::SetupNode& nd : mSetup.nodes) {
        if (nd.script || nd.category != 6) {
            continue;
        }
        switch (nd.id) {
            case 0x46:
                ++counts[0];
                break;
            case 0x2D:
                ++counts[1];
                break;
            case 0x47:
                ++counts[2];
                break;
            default:
                for (int j = 0; j < 5; ++j) {
                    if (nd.id == kJinjoActors[j]) {
                        jinjos[j] = true;
                    }
                }
                break;
        }
    }
    for (const Lightbulb::SetupProp& prop : mSetup.props) {
        if (prop.type == 0 && 0x572u + prop.id == 0x6D6u) {
            ++counts[3];
        }
    }

    // Background list, so panels floating over the level view still cover the icons.
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float textPx = kHudIconPx * 0.8f;
    const float rowPx = kHudIconPx + kHudGapPx;

    // Fit the icon in a square cell so the counts line up whatever shape the sprite is.
    auto drawIcon = [&](int slot, float cellX, float cellY, float cellPx, int alpha) {
        float texW = 0.0f, texH = 0.0f;
        void* tex = Lightbulb::O2rGuiTexture(kHudIcons[slot].name, texW, texH);
        if (!tex || texW <= 0.0f || texH <= 0.0f) {
            return;
        }
        const float fit = cellPx / ((texW > texH) ? texW : texH);
        const float w = texW * fit, h = texH * fit;
        const ImVec2 at(cellX + (cellPx - w) * 0.5f, cellY + (cellPx - h) * 0.5f);
        draw->AddImage((ImTextureID)tex, at, ImVec2(at.x + w, at.y + h), ImVec2(0, 0), ImVec2(1, 1),
                       IM_COL32(255, 255, 255, alpha));
    };

    // ImGui ships one weight, so the count is stamped a pixel apart to read as bold.
    auto drawCount = [&](float x, float y, const char* text) {
        draw->AddText(font, textPx, ImVec2(x + 2.0f, y + 2.0f), IM_COL32(0, 0, 0, 220), text);
        draw->AddText(font, textPx, ImVec2(x, y), IM_COL32_WHITE, text);
        draw->AddText(font, textPx, ImVec2(x + 1.0f, y), IM_COL32_WHITE, text);
        draw->AddText(font, textPx, ImVec2(x, y + 1.0f), IM_COL32_WHITE, text);
    };

    // The status bar sits over the bottom of the level view, so start above it.
    const float bottom = vy + vh - ImGui::GetFrameHeight() - kHudEdgeYPx;
    float y = bottom - kHudIconPx - 3.0f * rowPx;
    for (int i = 0; i < 4; ++i, y += rowPx) {
        drawIcon(i, vx + kHudEdgeXPx, y, kHudIconPx, 255);
        char text[16];
        std::snprintf(text, sizeof(text), "%d", counts[i]);
        drawCount(vx + kHudEdgeXPx + kHudIconPx + kHudGapPx, y + (kHudIconPx - textPx) * 0.5f, text);
    }

    // All five heads stay put, faded like the world tracker's when the map has none of that color.
    float x = vx + vw - kHudEdgeXPx - kHudJinjoPx;
    for (int j = 4; j >= 0; --j) {
        drawIcon(4 + j, x, bottom - kHudJinjoPx, kHudJinjoPx, jinjos[j] ? 255 : 102);
        x -= kHudJinjoPx + kHudGapPx;
    }
}

void App::DrawStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float barHeight = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - barHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, barHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 0.0f));
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(mStatus.c_str());
        if (mO2rLoaded) {
            const char* hint = "WASD/QE fly, Shift/Ctrl faster/slower, wheel sets speed, left-drag look, "
                               "right-click select, F focus, Del removes, Ctrl-Z undo";
            const ImGuiStyle& style = ImGui::GetStyle();
            const float sliderWidth = 150.0f;
            const float rightWidth = ImGui::CalcTextSize(hint).x + style.ItemSpacing.x + sliderWidth +
                                     style.ItemInnerSpacing.x + ImGui::CalcTextSize("move speed").x;
            const float rightX = ImGui::GetWindowWidth() - rightWidth - 8.0f;
            if (rightX > ImGui::GetCursorPosX()) {
                ImGui::SameLine(rightX);
            } else {
                ImGui::SameLine();
            }
            ImGui::TextDisabled("%s", hint);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(sliderWidth);
            int moveSpeed = (int)(mConfig.cameraSpeed / 10.0f + 0.5f) * 10;
            if (ImGui::SliderInt("move speed", &moveSpeed, 10, 100, "%d")) {
                mConfig.cameraSpeed = (float)((moveSpeed / 10) * 10);
                SaveSettings();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
