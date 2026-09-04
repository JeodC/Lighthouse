#include "App.h"

#include <fast/Fast3dWindow.h>
#include <fast/backends/gfx_rendering_api.h>
#include <ship/Context.h>
#include <ship/config/Config.h>
#include <ship/config/ConsoleVariable.h>
#include <ship/window/Window.h>

#include "imgui.h"

#include <filesystem>
#include <memory>
#include <string>

namespace {
const char* kFilterNames[] = { "Three-point (N64)", "Linear (bilinear)", "None (sharp pixels)" };
}

void App::DrawReloadOffer() {
    if (mO2rLoaded) {
        return;
    }

    // Poll for the file rather than the process: Torch in another window produces it too.
    if (mAwaitingExtraction) {
        const double now = ImGui::GetTime();
        if (now >= mNextArchivePoll) {
            mNextArchivePoll = now + 1.0;
            const std::string produced = Lightbulb::FindBaseArchive();
            if (!produced.empty()) {
                if (OpenO2rPath(produced)) {
                    mAwaitingExtraction = false;
                    return;
                }
                // Most likely still being written; try again on the next poll.
                mStatus = "Waiting for Lighthouse to finish writing bk.o2r...";
            }
        }
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Open bk.o2r", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Lightbulb edits a Banjo-Kazooie o2r. Open the bk.o2r "
                       "extracted by Torch to begin.");
    ImGui::Spacing();

    std::error_code statErr;
    const bool hasLast = !mConfig.lastO2rPath.empty() && std::filesystem::exists(mConfig.lastO2rPath, statErr);
    const std::string& offered = hasLast ? mConfig.lastO2rPath : mAdjacentO2rPath;
    if (!offered.empty()) {
        if (ImGui::Button(hasLast ? "Reopen last" : "Open bk.o2r")) {
            OpenO2rPath(offered);
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(offered.empty() ? "Open bk.o2r..." : "Open a different o2r...")) {
        OpenO2r();
    }
    if (!offered.empty()) {
        ImGui::TextDisabled("%s", offered.c_str());
    }

    ImGui::Separator();
    if (mAwaitingExtraction) {
        ImGui::TextWrapped("Lighthouse is running. Extract your ROM there and the archive opens here by itself.");
        if (ImGui::Button("Stop waiting")) {
            mAwaitingExtraction = false;
        }
    } else {
        ImGui::TextWrapped("Don't have one? Lighthouse builds a bk.o2r from your ROM.");
        if (ImGui::Button("Generate with Lighthouse...")) {
            std::string error;
            if (Lightbulb::LaunchLighthouse(error)) {
                mAwaitingExtraction = true;
                mNextArchivePoll = 0.0;
                mStatus = "Launched Lighthouse - extract your ROM there.";
            } else {
                mStatus = error;
            }
        }
    }

    if (!mStatus.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.4f, 1.0f), "%s", mStatus.c_str());
    }
    ImGui::End();
}

void App::DrawCredits() {
    if (!mShowCredits) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Credits", &mShowCredits)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Lightbulb");
    ImGui::TextWrapped("Level editor for the Lighthouse project, built on libultraship (Fast3D) and Dear ImGui.");

    ImGui::SeparatorText("Stand-in Models");
    ImGui::TextWrapped(
        "The editor's stand-in marker models for entry points, warps, camera triggers, boundaries "
        "and path nodes come from Banjo's Backpack, converted from its .mw resources. They were created by Tee-Hee.");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "License status: unknown.");
    ImGui::TextWrapped("These assets are not covered by Lighthouse's CC0 dedication. They are included with "
                       "attribution while their licensing is confirmed with their authors.");

    ImGui::End();
}

void App::DrawPreferences() {
    if (!mShowPreferences) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Preferences", &mShowPreferences)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Startup");
    if (ImGui::Checkbox("Open bk.o2r automatically", &mConfig.autoOpen)) {
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Loads the bk.o2r Lighthouse uses, when there is one.");
    }
    if (ImGui::Checkbox("Remember previous session", &mConfig.rememberSession)) {
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reopens the last romhack, level and camera position.");
    }

    ImGui::SeparatorText("Level view");
    if (ImGui::Checkbox("Apply actor spawn overrides", &mConfig.actorOverrides)) {
        Lightbulb::SetActorOverridesEnabled(mConfig.actorOverrides);
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw actors where their spawn code puts them, not at raw node values.");
    }
    if (ImGui::Checkbox("Draw actor models", &mConfig.actorModels)) {
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off shows a marker where each actor spawns instead.");
    }
    if (ImGui::Checkbox("Animate objects", &mConfig.animateObjects)) {
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Animate collectibles in the editor.");
    }
    if (ImGui::Checkbox("Play the level's music", &mConfig.autoPlayLevelMusic)) {
        if (!mConfig.autoPlayLevelMusic) {
            Lightbulb::StopLevelMusic();
        }
        SaveSettings();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Start a level's own track when you select it. Tools > Music for the rest.");
    }

    ImGui::SeparatorText("Display");
    auto backendName = [](int backendId) -> const char* {
        switch (backendId) {
            case 1:
                return "DirectX 11";
            case 2:
                return "OpenGL";
            case 3:
                return "Metal";
            default:
                return "Unknown";
        }
    };
    static bool sBackendChanged = false;
    if (auto* context = Ship::Context::GetRawInstance()) {
        if (auto window = context->GetWindow()) {
            bool fullscreen = window->IsFullscreen();
            if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
                window->SetFullscreen(fullscreen);
            }
            const int currentBackend = window->GetWindowBackend();
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("Graphics backend", backendName(currentBackend))) {
                if (auto backends = window->GetAvailableWindowBackends()) {
                    for (int backendId : *backends) {
                        if (ImGui::Selectable(backendName(backendId), backendId == currentBackend) &&
                            backendId != currentBackend) {
                            auto config = context->GetConfig();
                            config->SetInt("Window.Backend.Id", backendId);
                            config->SetString("Window.Backend.Name", backendName(backendId));
                            config->Save();
                            sBackendChanged = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            if (sBackendChanged) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Restart Lightbulb to apply the backend change.");
            }
        }
    }

    {
        auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
        int filterMode = cvars->GetInteger("gTextureFilter", (int)Fast::FILTER_THREE_POINT);
        if (filterMode < 0 || filterMode > 2) {
            filterMode = (int)Fast::FILTER_THREE_POINT;
        }
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("Texture filtering", kFilterNames[filterMode])) {
            for (int mode = 0; mode < 3; ++mode) {
                if (ImGui::Selectable(kFilterNames[mode], mode == filterMode) && mode != filterMode) {
                    cvars->SetInteger("gTextureFilter", mode);
                    cvars->Save();
                    if (auto fastWindow = std::dynamic_pointer_cast<Fast::Fast3dWindow>(
                            Ship::Context::GetRawInstance()->GetWindow())) {
                        fastWindow->SetTextureFilter((Fast::FilteringMode)mode);
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::End();
}
