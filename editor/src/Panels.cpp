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

    // Nothing was drawn for it this frame - hidden layer, culled, or past the pool limit.
    // The setup still knows where it sits.
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
    // Keep the current heading and only dolly, so focusing reads as travelling to the
    // object rather than being spun around to face it.
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
                ResumeLevelMusic();
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
    // The viewport pick below runs after the lists, so a reveal it asks for is consumed on
    // the next frame; clear it here once they have had their chance.
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
            float look[3], right[3];
            lookVectors(scene.yaw, scene.pitch, look, right);
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
            if (ImGui::IsKeyPressed(ImGuiKey_F))
                FocusSelection();
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
                ImGui::TextDisabled(prop.type == 2 ? "model" : prop.type == 0 ? "sprite" : "actor");
                ImGui::TableNextColumn();
                auto assetCell = [](const std::map<uint32_t, std::string>& index, uint32_t assetId, const char* kind) {
                    const auto found = index.find(assetId);
                    if (found != index.end()) {
                        ImGui::TextUnformatted(assetShortName(found->second).c_str());
                    } else {
                        ImGui::TextDisabled("%s %X (missing)", kind, assetId);
                    }
                };
                if (prop.type == 2) {
                    assetCell(mModelIndex, 0x2D1u + prop.id, "model");
                } else if (prop.type == 0) {
                    assetCell(mSpriteIndex, 0x572u + prop.id, "sprite");
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
    ImGui::TextDisabled("Fly: WASD / QE, left-drag to look, right-click to select, F to focus it");
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
            if (nd.category == 9 && nd.id == cam.index) {
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
        const bool isSpawn = node.category == 6;
        const char* actorName = isSpawn ? Lightbulb::ActorEnumName(node.id) : nullptr;
        ImGui::Text("Node #%d", mPropSel - propCount);
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
            ImGui::Text("Id          : 0x%X", node.id);
        } else {
            ImGui::Text("Id          : %u (0x%X)", node.id, node.id);
        }
        if (isSpawn && !Lightbulb::EditorEntryPointId(node.id) && Lightbulb::ActorIsSpawnable(node.id) &&
            mRomhackPath.empty() && mLevelScene.sel >= 0 && mLevelScene.sel < (int)mLevelScene.entries.size() &&
            !Lightbulb::ActorRegisteredForMap(mLevelScene.entries[mLevelScene.sel].mapId, node.id)) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f), "Never spawns  : map doesn't register this actor");
        }
        const uint32_t modelAsset = isSpawn ? Lightbulb::ActorModelAsset(node.id) : 0;
        if (modelAsset) {
            ImGui::Text("Model asset : %s", assetFullName(mModelIndex, modelAsset).c_str());
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
