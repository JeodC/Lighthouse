#include "App.h"
#include "LevelView.h"
#include "O2rImport.h"

#include "PreviewScene.h"
#include "UiCommon.h"
extern "C" {
#include "model.h"
}

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
extern int32_t gLbDlSolo;
extern int32_t gLbDlCount;
extern int32_t gLbDlOffsets[128];
extern int32_t gLbDlKinds[128];
extern int32_t gLbDlMaskEnable;
extern int32_t gLbDlMask[128];
extern int32_t gLbDepthModeOverride;
extern int32_t gLbDepthModeUsed;
}

namespace {
const char* kDepthModes[] = { "depth: caller", "depth: NONE", "depth: FULL", "depth: COMPARE" };
const char* kDepthUsed[] = { "NONE", "FULL", "COMPARE" };

const char* assetTail(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}
uint32_t assetId(const char* name) {
    return std::strncmp(name, "ASSET_", 6) == 0 ? (uint32_t)std::strtoul(name + 6, nullptr, 16) : 0;
}
} // namespace

void App::DrawModelViewer() {
    if (!mShowModels) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Models", &mShowModels)) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded) {
        ImGui::TextWrapped("Open a bk.o2r to browse its models.");
        if (ImGui::Button("Open bk.o2r...")) {
            OpenO2r();
        }
        ImGui::End();
        return;
    }

    DrawO2rBrowser("obj", "assets/model", mObjView);
    ImGui::End();
}

void App::DrawO2rBrowser(const char* idPrefix, const char* assetDir, O2rView& view) {
    if (view.paths.empty()) {
        view.paths = Lightbulb::ListO2rModelPaths(assetDir);
        const std::vector<std::string> editorModels = Lightbulb::ListO2rModelPaths("editor");
        view.paths.insert(view.paths.end(), editorModels.begin(), editorModels.end());
    }

    char id[96];
    std::snprintf(id, sizeof(id), "##%slist", idPrefix);
    ImGui::BeginChild(id, ImVec2(view.listW, 0), true);
    {
        const int clicked =
            Lightbulb::ui::AssetPicker(idPrefix, view.paths, view.filter, sizeof(view.filter), view.sel, "items");
        if (clicked >= 0) {
            view.sel = clicked;
            view.reframe = true;
            view.animSel = -1;
        }
    }
    ImGui::EndChild();

    std::snprintf(id, sizeof(id), "##%ssplit", idPrefix);
    Lightbulb::ui::VerticalSplitter(id, view.listW);
    std::snprintf(id, sizeof(id), "##%sview", idPrefix);
    ImGui::BeginChild(id, ImVec2(0, 0), false);
    {
        if (ImGui::Button("Reset camera")) {
            view.yaw = 30.0f;
            view.pitch = 20.0f;
            view.reframe = true;
        }
        ImGui::SameLine();
        Lightbulb::ui::TextDisabledWrapped("right-drag: orbit | wheel: zoom | WASD: pan");

        if (view.animPaths.empty()) {
            view.animPaths = Lightbulb::ListO2rAnimPaths();
        }
        {
            BKModelBin* model = (view.sel >= 0 && view.sel < (int)view.paths.size())
                                    ? Lightbulb::LoadO2rModel(view.paths[view.sel])
                                    : nullptr;
            const uint32_t modelAsset =
                (view.sel >= 0 && view.sel < (int)view.paths.size()) ? assetId(assetTail(view.paths[view.sel])) : 0;
            const bool skeletal = model && model->animation_list_offset != 0;
            const bool fromTable = skeletal && modelAsset && Lightbulb::ModelHasAnimTable(modelAsset);

            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##animfilter", "filter", view.animFilter, sizeof(view.animFilter));
            ImGui::SameLine();
            const char* curName = (view.animSel >= 0 && view.animSel < (int)view.animPaths.size())
                                      ? assetTail(view.animPaths[view.animSel])
                                      : "(static, no animation)";
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("##anim", curName)) {
                if (ImGui::Selectable("(static, no animation)", view.animSel < 0)) {
                    view.animSel = -1;
                }
                int shown = 0;
                for (int row = 0; skeletal && row < (int)view.animPaths.size(); ++row) {
                    const char* animName = assetTail(view.animPaths[row]);
                    if (!Lightbulb::ui::ContainsNoCase(animName, view.animFilter)) {
                        continue;
                    }
                    if (!view.animShowAll &&
                        !Lightbulb::ModelUsesAnim(modelAsset, assetId(assetTail(view.animPaths[row])))) {
                        continue;
                    }
                    char label[160];
                    std::snprintf(label, sizeof(label), "%s##anim%d", animName, row);
                    if (ImGui::Selectable(label, row == view.animSel)) {
                        view.animSel = row;
                        view.animProgress = 0.0f;
                        view.animPlay = true;
                        const float tableDuration = Lightbulb::AnimDuration(modelAsset, assetId(animName));
                        if (tableDuration > 0.0f && tableDuration < 600.0f) {
                            view.animDuration = tableDuration;
                        } else if (const Lightbulb::O2rAnim* anim = Lightbulb::LoadO2rAnim(view.animPaths[row]);
                                   anim && anim->endFrame > anim->startFrame) {
                            view.animDuration = (float)(anim->endFrame - anim->startFrame) / 30.0f;
                        }
                    }
                    ++shown;
                }
                if (shown == 0) {
                    ImGui::TextDisabled(!skeletal          ? "  (this model has no skeleton)"
                                        : view.animShowAll ? "  (no matches)"
                                                           : "  (none known for this model)");
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::Checkbox("all", &view.animShowAll);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Off: only animations the decomp gives this model.");
            }
            if (model && skeletal) {
                ImGui::SameLine();
                if (fromTable) {
                    Lightbulb::ui::TextDisabledWrapped("(from the decomp's actor table)");
                } else {
                    ImGui::TextDisabled("(none known)");
                }
            }
            if (view.animSel >= 0) {
                ImGui::SameLine();
                if (ImGui::Button(view.animPlay ? "Pause" : "Play")) {
                    view.animPlay = !view.animPlay;
                }
                ImGui::SameLine();
                ImGui::Checkbox("loop", &view.animLoop);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderFloat("##scrub", &view.animProgress, 0.0f, 1.0f, "t %.2f")) {
                    view.animPlay = false;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                ImGui::DragFloat("loop secs", &view.animDuration, 0.05f, 0.1f, 30.0f, "%.2f");
            }
        }
        {
            const int last = gLbDlCount > 0 ? gLbDlCount - 1 : 0;
            if (ImGui::ArrowButton("##dlprev", ImGuiDir_Left) && gLbDlSolo > -1) {
                gLbDlSolo--;
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##dlnext", ImGuiDir_Right) && gLbDlSolo < last) {
                gLbDlSolo++;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::SliderInt("##dlsolo", &gLbDlSolo, -1, last, gLbDlSolo < 0 ? "sub-DL: all" : "sub-DL: %d");
            ImGui::SameLine();
            if (gLbDlSolo >= 0 && gLbDlSolo < gLbDlCount && gLbDlSolo < 128) {
                ImGui::TextDisabled("of %d | geoCmd%d, DL offset %d", gLbDlCount, gLbDlKinds[gLbDlSolo],
                                    gLbDlOffsets[gLbDlSolo]);
            } else {
                ImGui::TextDisabled("%d sub-DLs", gLbDlCount);
            }

            ImGui::SameLine();
            {
                int sel = gLbDepthModeOverride + 1;
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::Combo("##depthmode", &sel, kDepthModes, 4)) {
                    gLbDepthModeOverride = sel - 1;
                }
                if (ImGui::IsItemHovered() && gLbDepthModeUsed >= 0) {
                    ImGui::SetTooltip("last draw used: %s", gLbDepthModeUsed <= 2 ? kDepthUsed[gLbDepthModeUsed] : "?");
                }
            }

            ImGui::SameLine();
            bool maskOn = gLbDlMaskEnable != 0;
            if (ImGui::Checkbox("multi", &maskOn)) {
                gLbDlMaskEnable = maskOn ? 1 : 0;
                if (maskOn) {
                    for (int subDl = 0; subDl < 128; subDl++) {
                        gLbDlMask[subDl] = (gLbDlSolo < 0 || gLbDlSolo == subDl) ? 1 : 0;
                    }
                }
            }
            if (gLbDlMaskEnable) {
                if (ImGui::SmallButton("all")) {
                    for (int subDl = 0; subDl < 128; subDl++) {
                        gLbDlMask[subDl] = 1;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("none")) {
                    for (int subDl = 0; subDl < 128; subDl++) {
                        gLbDlMask[subDl] = 0;
                    }
                }
                ImGui::SameLine();
                int shown = 0;
                for (int subDl = 0; subDl < gLbDlCount && subDl < 128; subDl++) {
                    shown += gLbDlMask[subDl] ? 1 : 0;
                }
                ImGui::TextDisabled("%d/%d shown", shown, gLbDlCount);
                const float wrapX = ImGui::GetContentRegionAvail().x;
                float rowWidth = 0.0f;
                for (int subDl = 0; subDl < gLbDlCount && subDl < 128; subDl++) {
                    char label[16];
                    std::snprintf(label, sizeof(label), "%d", subDl);
                    const float buttonWidth = ImGui::CalcTextSize(label).x + 12.0f;
                    if (subDl > 0 && rowWidth + buttonWidth < wrapX) {
                        ImGui::SameLine();
                    } else {
                        rowWidth = 0.0f;
                    }
                    rowWidth += buttonWidth + 4.0f;
                    const bool visible = gLbDlMask[subDl] != 0;
                    if (!visible) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                    }
                    if (ImGui::SmallButton(label)) {
                        gLbDlMask[subDl] = visible ? 0 : 1;
                    }
                    if (!visible) {
                        ImGui::PopStyleColor(2);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("sub-DL %d: geoCmd%d, offset %d", subDl, gLbDlKinds[subDl],
                                          gLbDlOffsets[subDl]);
                    }
                }
            }
        }

        const double now = ImGui::GetTime();
        const double elapsed = now - view.animLastTime;
        view.animLastTime = now;
        if (view.animSel >= 0 && view.animPlay && view.animDuration > 0.0f) {
            view.animProgress += (float)(elapsed / view.animDuration);
            if (view.animLoop) {
                view.animProgress -= std::floor(view.animProgress);
            } else if (view.animProgress >= 1.0f) {
                view.animProgress = 1.0f;
                view.animPlay = false;
            }
        }

        ImVec2 region = ImGui::GetContentRegionAvail();
        int viewWidth = (int)region.x;
        int viewHeight = (int)region.y;
        if (viewWidth > 16 && viewHeight > 16) {
            BKModelBin* model = nullptr;
            if (view.sel >= 0 && view.sel < (int)view.paths.size()) {
                model = Lightbulb::LoadO2rModel(view.paths[view.sel]);
            }
            float modelRadius = 1000.0f;
            if (model && model->vtx_list_offset != 0) {
                const BKVertexList* vl = modelbin_getVtxList(model);
                modelRadius = (float)(vl->global_norm > 0 ? vl->global_norm : 1000);
                if (view.reframe) {
                    view.center[0] = (float)(vl->minCoord[0] + vl->maxCoord[0]) * 0.5f;
                    view.center[1] = (float)(vl->minCoord[1] + vl->maxCoord[1]) * 0.5f;
                    view.center[2] = (float)(vl->minCoord[2] + vl->maxCoord[2]) * 0.5f;
                    view.dist = modelRadius * 3.5f;
                    view.reframe = false;
                }
            }
            Lightbulb::ModelDrawParams drawParams;
            drawParams.yawDeg = view.yaw;
            drawParams.pitchDeg = view.pitch;
            drawParams.center[0] = view.center[0];
            drawParams.center[1] = view.center[1];
            drawParams.center[2] = view.center[2];
            drawParams.distance = view.dist;
            drawParams.radius = modelRadius;
            Mtx boneMtx[256];
            int boneN = 0;
            if (model && view.animSel >= 0 && view.animSel < (int)view.animPaths.size()) {
                if (const Lightbulb::O2rAnim* anim = Lightbulb::LoadO2rAnim(view.animPaths[view.animSel])) {
                    static std::vector<Lightbulb::BonePose> poses;
                    Lightbulb::SampleO2rAnim(*anim, view.animProgress, poses);
                    boneN = Lightbulb::BuildBoneMatrices(model, poses, boneMtx, 256);
                    if (boneN > 0) {
                        drawParams.boneMtx = boneMtx;
                        drawParams.boneCount = boneN;
                    }
                }
            }
            if (model) {
                Lightbulb::TransformAnimVertices(model, boneN > 0 ? boneMtx : nullptr, boneN);
            }
            void* tex = Lightbulb::RenderModelPreview(model, viewWidth, viewHeight, drawParams, 0);
            if (tex) {
                ImGui::Image((ImTextureID)tex, ImVec2((float)viewWidth, (float)viewHeight));
            } else {
                ImGui::Dummy(ImVec2((float)viewWidth, (float)viewHeight));
            }
            if (ImGui::IsItemHovered()) {
                ImGuiIO& io = ImGui::GetIO();
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    view.yaw += io.MouseDelta.x * 0.5f;
                    view.pitch += io.MouseDelta.y * 0.5f;
                }
                if (io.MouseWheel != 0.0f) {
                    view.dist -= io.MouseWheel * view.dist * 0.1f;
                }
                float pan = view.dist * 0.01f;
                if (pan < 1.0f) {
                    pan = 1.0f;
                }
                const float yawRad = view.yaw * Lightbulb::kDeg;
                const float yawCos = std::cos(yawRad);
                const float yawSin = std::sin(yawRad);
                if (ImGui::IsKeyDown(ImGuiKey_W)) {
                    view.center[1] += pan;
                }
                if (ImGui::IsKeyDown(ImGuiKey_S)) {
                    view.center[1] -= pan;
                }
                if (ImGui::IsKeyDown(ImGuiKey_A)) {
                    view.center[0] -= yawCos * pan;
                    view.center[2] -= yawSin * pan;
                }
                if (ImGui::IsKeyDown(ImGuiKey_D)) {
                    view.center[0] += yawCos * pan;
                    view.center[2] += yawSin * pan;
                }
                if (view.dist < 50.0f) {
                    view.dist = 50.0f;
                }
            }
        }
    }
    ImGui::EndChild();
}
