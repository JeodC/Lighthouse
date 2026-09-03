#include "App.h"
#include "O2rImport.h"

#include "PreviewScene.h"
#include "UiCommon.h"

#include "imgui.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr int kThumbPx = 96;
constexpr int kChunkPx = 64;
constexpr double kPreviewFps = 8.0;

} // namespace

void App::DrawSpriteViewer() {
    if (!mShowSprites) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Sprites", &mShowSprites)) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded) {
        ImGui::TextWrapped("Open a bk.o2r to browse its sprites.");
        if (ImGui::Button("Open bk.o2r...")) {
            OpenO2r();
        }
        ImGui::End();
        return;
    }

    SpriteView& view = mSpriteView;
    if (view.paths.empty()) {
        view.paths = Lightbulb::ListO2rSpritePaths();
    }

    ImGui::BeginChild("##sprlist", ImVec2(view.listW, 0), true);
    {
        const int clicked =
            Lightbulb::ui::AssetPicker("spr", view.paths, view.filter, sizeof(view.filter), view.sel, "sprites");
        if (clicked >= 0) {
            view.sel = clicked;
            view.animTime = 0.0;
            view.manualFrame = -1;
            view.play = true;
        }
    }
    ImGui::EndChild();

    Lightbulb::ui::VerticalSplitter("##sprsplit", view.listW);

    ImGui::BeginChild("##sprview", ImVec2(0, 0), false);
    {
        const Lightbulb::O2rSpriteTex* loaded = (view.sel >= 0 && view.sel < (int)view.paths.size())
                                                    ? Lightbulb::LoadO2rSpriteByPath(view.paths[view.sel])
                                                    : nullptr;
        if (!loaded) {
            ImGui::TextDisabled("(Select a sprite)");
            ImGui::EndChild();
            ImGui::End();
            return;
        }
        const Lightbulb::O2rSpriteTex& sprite = *loaded;

        const int frameCount = (int)sprite.frames.size();
        {
            const std::string& path = view.paths[view.sel];
            const size_t slash = path.find_last_of('/');
            ImGui::TextUnformatted(path.c_str() + (slash == std::string::npos ? 0 : slash + 1));
        }
        ImGui::TextDisabled("%d frames | anim type %d, speed %d | f0 %dx%d px "
                            "| display %.0fx%.0f",
                            frameCount, sprite.animType, sprite.animSpeed, sprite.frames[0].frameW,
                            sprite.frames[0].frameH, sprite.dispW, sprite.dispH);
        const bool animatesItself = (sprite.animType != 0 && sprite.animSpeed != 0 && frameCount > 1);
        if (!animatesItself && frameCount > 1) {
            ImGui::SameLine();
            Lightbulb::ui::TextDisabledWrapped("(code-driven; preview cycling)");
        }

        if (view.thumbSel != view.sel || view.thumbPx != kThumbPx) {
            view.thumbTex.assign(frameCount, nullptr);
            for (int frame = 0; frame < frameCount; ++frame) {
                view.thumbTex[frame] =
                    Lightbulb::RenderSpritePreview(sprite, frame, false, kThumbPx, kThumbPx, 0.0f, 0.0f, 100 + frame);
            }
            view.thumbSel = view.sel;
            view.thumbPx = kThumbPx;
        }

        const double now = ImGui::GetTime();
        if (view.play) {
            view.animTime += now - view.lastTime;
        }
        view.lastTime = now;
        int shownFrame = 0;
        bool shownMirrored = false;
        if (view.manualFrame >= 0 && view.manualFrame < frameCount) {
            shownFrame = view.manualFrame;
        } else if (animatesItself) {
            const Lightbulb::SpriteFrame playing = Lightbulb::SpriteFrameAt(sprite, view.animTime, 0);
            shownFrame = playing.frame;
            shownMirrored = playing.mirror;
        } else if (frameCount > 1) {
            shownFrame = (int)(view.animTime * kPreviewFps) % frameCount;
        }

        const int blockCount = (int)sprite.frames[shownFrame].chunks.size();
        if (blockCount > 1) {
            ImGui::Checkbox("Split into blocks", &view.showChunks);
            ImGui::SameLine();
            ImGui::TextDisabled("(%d in f%d)", blockCount, shownFrame);
        }
        const bool blockView = view.showChunks && blockCount > 1;
        if (blockView && view.chunkKey != view.sel * 4096 + shownFrame) {
            view.chunkTex.assign(blockCount, nullptr);
            for (int block = 0; block < blockCount; ++block) {
                view.chunkTex[block] = Lightbulb::RenderSpritePreview(sprite, shownFrame, false, kChunkPx, kChunkPx,
                                                                      0.0f, 0.0f, 1000 + block, block);
            }
            view.chunkKey = view.sel * 4096 + shownFrame;
        }

        const float previewPx = 200.0f;
        const ImVec2 region = ImGui::GetContentRegionAvail();
        float gridHeight = region.y - previewPx - 8.0f;
        if (gridHeight < 80.0f) {
            gridHeight = 80.0f;
        }
        ImGui::BeginChild("##frames", ImVec2(0.0f, gridHeight), true);
        {
            const int cellPx = blockView ? kChunkPx : kThumbPx;
            const int count = blockView ? blockCount : frameCount;
            const std::vector<void*>& tex = blockView ? view.chunkTex : view.thumbTex;
            const float stride = (float)cellPx + 10.0f;
            int perRow = (int)(ImGui::GetContentRegionAvail().x / stride);
            if (perRow < 1) {
                perRow = 1;
            }
            for (int cell = 0; cell < count; ++cell) {
                void* cellTex = (cell < (int)tex.size()) ? tex[cell] : nullptr;
                if (cellTex) {
                    ImGui::Image((ImTextureID)cellTex, ImVec2((float)cellPx, (float)cellPx));
                } else {
                    ImGui::Dummy(ImVec2((float)cellPx, (float)cellPx));
                }
                if (blockView) {
                    if (ImGui::IsItemHovered()) {
                        const Lightbulb::O2rSpriteChunk& block = sprite.frames[shownFrame].chunks[cell];
                        ImGui::SetTooltip("block %d: %dx%d at %d,%d", cell, block.width, block.height, block.posX,
                                          block.posY);
                    }
                } else if (cell == shownFrame) {
                    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                        IM_COL32(255, 216, 51, 255), 0.0f, 0, 2.0f);
                }
                if ((cell + 1) % perRow != 0 && cell + 1 < count) {
                    ImGui::SameLine();
                }
            }
        }
        ImGui::EndChild();

        ImGui::BeginGroup();
        {
            ImGui::Checkbox("play", &view.play);
            if (frameCount > 1) {
                int shown = shownFrame;
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::SliderInt("frame", &shown, 0, frameCount - 1)) {
                    view.manualFrame = shown;
                    view.play = false;
                }
                if (ImGui::SmallButton("auto")) {
                    view.manualFrame = -1;
                    view.play = true;
                }
            }
            ImGui::TextDisabled("showing f%d%s", shownFrame, shownMirrored ? " (mirrored)" : "");
        }
        ImGui::EndGroup();

        ImGui::SameLine();
        const float rightX = ImGui::GetWindowContentRegionMax().x - previewPx;
        if (rightX > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(rightX);
        }
        void* previewTex = Lightbulb::RenderSpritePreview(sprite, shownFrame, shownMirrored, (int)previewPx,
                                                          (int)previewPx, 0.0f, 0.0f, 2);
        if (previewTex) {
            ImGui::Image((ImTextureID)previewTex, ImVec2(previewPx, previewPx));
        } else {
            ImGui::Dummy(ImVec2(previewPx, previewPx));
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
