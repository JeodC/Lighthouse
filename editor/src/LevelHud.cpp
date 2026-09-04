#include "App.h"
#include "O2rImport.h"
#include "PreviewScene.h"

#include "imgui.h"

#include <cstdio>

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

    // All five heads stay put; the colors this map has none of are faded.
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
