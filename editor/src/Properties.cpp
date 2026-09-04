#include "App.h"
#include "O2rImport.h"
#include "SetupNames.h"
#include "UiCommon.h"

#include "imgui.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

namespace {
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
// Names for the two 3-bit switches in func_803422D4, the routine that applies a waypoint.
const char* legHeadingModeName(uint8_t mode) {
    static const char* kNames[] = {
        "unchanged",        "face along the path", "stop using yaw",       "use yaw",
        "stop using pitch", "use pitch",           "stop using yaw+pitch", "use yaw+pitch",
    };
    return mode < 8 ? kNames[mode] : "?";
}
const char* legAnimModeName(uint8_t mode) {
    static const char* kNames[] = { "unchanged", "unchanged",     "play once", "play once reversed",
                                    "loop",      "loop reversed", "hold",      "unchanged" };
    return mode < 8 ? kNames[mode] : "?";
}
} // namespace

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
        // Yaw in a node's own range, 0-359, so it compares against Yaw (raw) below.
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
        ImGui::Text("Type        : %s (%u)", Lightbulb::CameraTypeName(cam.type), (unsigned)cam.type);
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
        ImGui::Text("Category    : %s (%u)", Lightbulb::NodeCategoryName(node.category), (unsigned)node.category);
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
                    node.id, Lightbulb::CameraTypeName(target->type));
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
    ImGui::Text("Placed object #%d", mPropSel);
    ImGui::Separator();
    ImGui::Text("Type        : %s", Lightbulb::PropKindName(prop.type));
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
    Lightbulb::ui::TextDisabledWrapped("Moving and editing placed objects, and writing the setup back to the o2r, "
                                       "come next.");
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
