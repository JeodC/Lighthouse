#include "App.h"
#include "O2rImport.h"
#include "UiCommon.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int TrackIdFromPath(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    if (std::strncmp(name, "COMUSIC_", 8) != 0) {
        return -1;
    }
    return (int)std::strtoul(name + 8, nullptr, 16);
}

const char* TrackName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    if (std::strncmp(name, "COMUSIC_", 8) == 0) {
        if (const char* underscore = std::strchr(name + 8, '_')) {
            return underscore + 1;
        }
    }
    return name;
}

} // namespace

void App::ResumeLevelMusic() {
    if (mShowMusic || !mConfig.autoPlayLevelMusic) {
        return;
    }
    if (mLevelScene.sel < 0 || mLevelScene.sel >= (int)mLevelScene.entries.size()) {
        return;
    }
    Lightbulb::StartLevelMusic(mLevelScene.entries[mLevelScene.sel].mapId);
}

void App::DrawMusicViewer() {
    if (mShowMusic != mMusicPanelOpen) {
        mMusicPanelOpen = mShowMusic;
        if (mShowMusic) {
            Lightbulb::StopMusic();
            mMusicView.playing = -1;
        } else {
            Lightbulb::StopMusic();
            mMusicView.playing = -1;
            ResumeLevelMusic();
        }
    }
    if (!mShowMusic) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Music", &mShowMusic)) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded) {
        ImGui::TextWrapped("Open a bk.o2r to browse its music.");
        if (ImGui::Button("Open bk.o2r...")) {
            OpenO2r();
        }
        ImGui::End();
        return;
    }

    MusicView& view = mMusicView;
    if (view.paths.empty()) {
        view.paths = Lightbulb::ListO2rResourcePaths("comusic");
    }

    ImGui::BeginChild("##musiclist", ImVec2(view.listW, 0), true);
    {
        const int clicked =
            Lightbulb::ui::AssetPicker("mus", view.paths, view.filter, sizeof(view.filter), view.sel, "tracks");
        if (clicked >= 0) {
            view.sel = clicked;
            if (view.autoPlay) {
                const int track = TrackIdFromPath(view.paths[view.sel]);
                if (track >= 0) {
                    Lightbulb::PlayMusicTrack(track);
                    view.playing = track;
                }
            }
        }
    }
    ImGui::EndChild();

    Lightbulb::ui::VerticalSplitter("##mussplit", view.listW);

    ImGui::BeginChild("##musview", ImVec2(0, 0), false);
    {
        const bool haveLevel = mLevelScene.sel >= 0 && mLevelScene.sel < (int)mLevelScene.entries.size();
        const int levelMap = haveLevel ? mLevelScene.entries[mLevelScene.sel].mapId : -1;
        ImGui::TextDisabled("engine %s   listener map %X   channels %04X   level map %X track %d   autoplay %s",
                            Lightbulb::MusicSlotIdle() ? "idle" : "playing", Lightbulb::MusicListenerMap(),
                            (unsigned)Lightbulb::MusicChannelMask(), levelMap,
                            haveLevel ? Lightbulb::LevelMusicTrack((uint16_t)levelMap) : -1,
                            mConfig.autoPlayLevelMusic ? "on" : "off");
        ImGui::Separator();

        if (view.sel < 0 || view.sel >= (int)view.paths.size()) {
            Lightbulb::ui::TextDisabledWrapped("Select a track to play it.");
            ImGui::EndChild();
            ImGui::End();
            return;
        }
        const std::string& path = view.paths[view.sel];
        const int track = TrackIdFromPath(path);

        ImGui::TextUnformatted(TrackName(path));
        ImGui::Separator();

        if (!Lightbulb::AudioEngineRunning()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Audio engine isn't running.");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        if (ImGui::Button("Play")) {
            Lightbulb::PlayMusicTrack(track);
            view.playing = track;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            Lightbulb::StopMusic();
            view.playing = -1;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Play on select", &view.autoPlay);

        ImGui::Spacing();
        if (ImGui::BeginTable("##musspec", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            auto field = [](const char* label, const char* fmt, ...) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", label);
                ImGui::TableSetColumnIndex(1);
                va_list args;
                va_start(args, fmt);
                ImGui::TextV(fmt, args);
                va_end(args);
            };
            field("Track", "%d (0x%X)", track, track);
            field("Resource", "%s", path.c_str());
            field("Playing", "%s", view.playing == track ? "yes" : "no");

            ImGui::EndTable();
        }

        if (!mRomhackPath.empty()) {
            ImGui::Spacing();
            Lightbulb::ui::TextDisabledWrapped("A romhack o2r is layered on top, so a track it replaces "
                                               "plays here as the romhack has it.");
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
