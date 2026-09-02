#include "App.h"

#include "port/FilePicker.h"
#include "O2rImport.h"

#include <cstring>
#include <ship/Context.h>
#include <ship/config/ConsoleVariable.h>
#include <spdlog/spdlog.h>

#include "imgui.h"
#include "imgui_internal.h"

#include <filesystem>
#include <string>

namespace {
constexpr const char* kLastO2rPath = "gLightbulb.LastO2rPath";
constexpr const char* kCameraSpeed = "gLightbulb.CameraSpeed";
constexpr const char* kActorOverrides = "gLightbulb.ActorOverrides";
constexpr const char* kAutoPlayLevelMusic = "gLightbulb.AutoPlayLevelMusic";
constexpr const char* kLayers = "gLightbulb.Layers";
constexpr const char* kAnimateObjects = "gLightbulb.AnimateObjects";
constexpr const char* kAutoOpen = "gLightbulb.AutoOpen";
constexpr const char* kActorModels = "gLightbulb.ActorModels";
constexpr const char* kRememberSession = "gLightbulb.RememberSession";
constexpr const char* kLastRomhackPath = "gLightbulb.LastRomhackPath";
constexpr const char* kLastMap = "gLightbulb.LastMap";
constexpr const char* kCamX = "gLightbulb.CamX";
constexpr const char* kCamY = "gLightbulb.CamY";
constexpr const char* kCamZ = "gLightbulb.CamZ";
constexpr const char* kCamYaw = "gLightbulb.CamYaw";
constexpr const char* kCamPitch = "gLightbulb.CamPitch";
} // namespace

namespace Lightbulb {
std::string FindBaseArchive() {
    const std::string path = Ship::Context::LocateFileAcrossAppDirs("bk.o2r", "bk");
    std::error_code statErr;
    return std::filesystem::exists(path, statErr) ? path : std::string();
}
} // namespace Lightbulb

App::App() {
    Lightbulb::LoadConfig(mConfig);
    Lightbulb::SetActorOverridesEnabled(mConfig.actorOverrides);

    const char* ini = ImGui::GetIO().IniFilename;
    std::error_code statErr;
    mFreshLayout = (ini == nullptr) || (ini[0] == '\0') || !std::filesystem::exists(ini, statErr);

    // Opening the base drops the remembered layer, so keep the session from before it does.
    const Lightbulb::Config saved = mConfig;
    mAdjacentO2rPath = Lightbulb::FindBaseArchive();
    if (mConfig.autoOpen && !mAdjacentO2rPath.empty()) {
        OpenO2rPath(mAdjacentO2rPath);
    }
    if (mO2rLoaded && mConfig.rememberSession) {
        RestoreSession(saved);
    }
}

void App::RestoreSession(const Lightbulb::Config& saved) {
    std::error_code statErr;
    if (!saved.lastRomhackPath.empty() && std::filesystem::exists(saved.lastRomhackPath, statErr)) {
        OpenRomhackPath(saved.lastRomhackPath);
    }
    if (saved.lastMapId < 0) {
        return;
    }
    EnsureLevelEntries();
    for (int row = 0; row < (int)mLevelScene.entries.size(); ++row) {
        if ((int)mLevelScene.entries[row].mapId != saved.lastMapId) {
            continue;
        }
        SelectLevel(row);
        for (int axis = 0; axis < 3; ++axis) {
            mLevelScene.eye[axis] = saved.lastEye[axis];
        }
        mLevelScene.yaw = saved.lastYaw;
        mLevelScene.pitch = saved.lastPitch;
        mLevelScene.framed = true;
        break;
    }
}

void App::SaveSession() {
    if (mLevelScene.sel >= 0 && mLevelScene.sel < (int)mLevelScene.entries.size()) {
        mConfig.lastMapId = mLevelScene.entries[mLevelScene.sel].mapId;
        for (int axis = 0; axis < 3; ++axis) {
            mConfig.lastEye[axis] = mLevelScene.eye[axis];
        }
        mConfig.lastYaw = mLevelScene.yaw;
        mConfig.lastPitch = mLevelScene.pitch;
    }
    SaveSettings();
}

App::~App() = default;

void App::SaveSettings() {
    Lightbulb::SaveConfig(mConfig);
}

void App::OpenO2r() {
    std::string path;
    if (!Lightbulb::OpenFileDialog("Open Banjo-Kazooie o2r", { { "Banjo-Kazooie o2r", { "*.o2r" } } }, path)) {
        return;
    }
    OpenO2rPath(path);
}

void App::OpenRomhackO2r() {
    std::string path;
    if (!Lightbulb::OpenFileDialog("Open romhack o2r", { { "Romhack o2r", { "*.o2r" } } }, path)) {
        return;
    }
    OpenRomhackPath(path);
}

void App::ResetLoadedScene() {
    mObjView = O2rView{};
    mSpriteView = SpriteView{};
    mSoundView = SoundView{};
    mLevelScene = LevelScene{};
    mSetup = Lightbulb::SetupScene{};
    mPropSel = -1;
    mModelIndex.clear();
    mSpriteIndex.clear();
    mPickTargets.clear();
}

bool App::OpenO2rPath(const std::string& path) {
    ResetLoadedScene();
    mRomhackPath.clear();
    mConfig.lastRomhackPath.clear();
    if (Lightbulb::MountO2r(path) != Lightbulb::MountResult::Base) {
        mO2rLoaded = false;
        mStatus = "Not a Banjo-Kazooie bk.o2r (no aBKAssetTable): " + path;
        return false;
    }
    mO2rPath = path;
    mO2rLoaded = true;
    mConfig.lastO2rPath = path;
    SaveSettings();
    mStatus = "Loaded " + path;
    Lightbulb::StartAudioEngine();
    return true;
}

bool App::OpenRomhackPath(const std::string& path) {
    ResetLoadedScene();
    const Lightbulb::MountResult result = Lightbulb::MountRomhackO2r(path);
    if (result == Lightbulb::MountResult::NeedsBase) {
        mStatus = "Open a bk.o2r before layering a romhack on top of it.";
        return false;
    }
    if (result != Lightbulb::MountResult::Romhack) {
        mStatus = "Couldn't read that romhack o2r: " + path;
        return false;
    }
    mRomhackPath = path;
    mConfig.lastRomhackPath = path;
    SaveSettings();
    Lightbulb::ReleaseMusicTracks();
    mMusicView.paths.clear();
    mMusicView.playing = -1;
    mStatus = "Loaded romhack " + path;
    return true;
}

void App::DrawFrame() {
    Lightbulb::PumpAudioEngine();
    DrawMenuBar();
    DrawLevelsPanel();
    DrawLayersPanel();
    DrawPropertiesPanel();
    DrawModelViewer();
    DrawSpriteViewer();
    DrawSoundViewer();
    DrawMusicViewer();
    DrawReloadOffer();
    DrawPreferences();
    DrawCredits();
    DrawStatusBar();
}

void App::EnforceDefaultLayout() {
    if (mLayoutInitialized) {
        return;
    }
    mLayoutInitialized = true;

    const ImGuiID dockId = ImHashStr("main_dock", 0, ImHashStr("Main - Deck"));
    if (!mFreshLayout) {
        return;
    }

    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_NoTabBar);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockId;
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.108f, nullptr, &center);
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.142f, nullptr, &center);
    const ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.198f, nullptr, &right);

    // The prompt stays floating: a dock node can't size itself to its content.
    ImGui::DockBuilderDockWindow("Levels", left);
    ImGui::DockBuilderDockWindow("Layers", rightTop);
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow("Main Game", center);

    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(left)) {
        node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    }
    ImGui::DockBuilderFinish(dockId);
}

void App::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open bk.o2r...")) {
            OpenO2r();
        }
        if (ImGui::MenuItem("Reopen Last bk.o2r", nullptr, false, !mConfig.lastO2rPath.empty())) {
            OpenO2rPath(mConfig.lastO2rPath);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open romhack o2r...", nullptr, false, Lightbulb::BaseO2rMounted())) {
            OpenRomhackO2r();
        }
        if (!Lightbulb::BaseO2rMounted() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Open a bk.o2r first: romhacks layer over the base game.");
        }
        if (ImGui::MenuItem("Unload romhack", nullptr, false, !mRomhackPath.empty())) {
            OpenO2rPath(mO2rPath);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            RequestClose();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        ImGui::MenuItem("Models...", nullptr, &mShowModels, mO2rLoaded);
        ImGui::MenuItem("Sprites...", nullptr, &mShowSprites, mO2rLoaded);
        ImGui::MenuItem("Sounds...", nullptr, &mShowSounds, mO2rLoaded);
        ImGui::MenuItem("Music...", nullptr, &mShowMusic, mO2rLoaded);
        ImGui::Separator();
        ImGui::MenuItem("Preferences...", nullptr, &mShowPreferences);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::BeginMenu("About")) {
            ImGui::Text("Lightbulb");
            ImGui::TextDisabled("Banjo-Kazooie level editor");
            ImGui::Separator();
            ImGui::TextWrapped("In-house Banjo-Kazooie level editor for the Lighthouse project. "
                               "Built on libultraship (Fast3D) and Dear ImGui.");
            ImGui::Separator();
            if (ImGui::MenuItem("Credits...")) {
                mShowCredits = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

namespace Lightbulb {
bool LoadConfig(Config& out) {
    auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    if (!cvars) {
        return false;
    }
    out.lastO2rPath = cvars->GetString(kLastO2rPath, "");
    out.cameraSpeed = cvars->GetFloat(kCameraSpeed, out.cameraSpeed);
    out.actorOverrides = cvars->GetInteger(kActorOverrides, out.actorOverrides ? 1 : 0) != 0;
    out.autoPlayLevelMusic = cvars->GetInteger(kAutoPlayLevelMusic, out.autoPlayLevelMusic ? 1 : 0) != 0;
    out.layers = (uint32_t)cvars->GetInteger(kLayers, (int32_t)out.layers);
    out.animateObjects = cvars->GetInteger(kAnimateObjects, out.animateObjects ? 1 : 0) != 0;
    out.autoOpen = cvars->GetInteger(kAutoOpen, out.autoOpen ? 1 : 0) != 0;
    out.actorModels = cvars->GetInteger(kActorModels, out.actorModels ? 1 : 0) != 0;
    out.rememberSession = cvars->GetInteger(kRememberSession, out.rememberSession ? 1 : 0) != 0;
    out.lastRomhackPath = cvars->GetString(kLastRomhackPath, "");
    out.lastMapId = cvars->GetInteger(kLastMap, -1);
    out.lastEye[0] = cvars->GetFloat(kCamX, 0.0f);
    out.lastEye[1] = cvars->GetFloat(kCamY, 0.0f);
    out.lastEye[2] = cvars->GetFloat(kCamZ, 0.0f);
    out.lastYaw = cvars->GetFloat(kCamYaw, 0.0f);
    out.lastPitch = cvars->GetFloat(kCamPitch, 0.0f);
    return true;
}

bool SaveConfig(const Config& cfg) {
    auto cvars = Ship::Context::GetRawInstance()->GetConsoleVariables();
    if (!cvars) {
        return false;
    }
    cvars->SetString(kLastO2rPath, cfg.lastO2rPath.c_str());
    cvars->SetFloat(kCameraSpeed, cfg.cameraSpeed);
    cvars->SetInteger(kActorOverrides, cfg.actorOverrides ? 1 : 0);
    cvars->SetInteger(kAutoPlayLevelMusic, cfg.autoPlayLevelMusic ? 1 : 0);
    cvars->SetInteger(kLayers, (int32_t)cfg.layers);
    cvars->SetInteger(kAnimateObjects, cfg.animateObjects ? 1 : 0);
    cvars->SetInteger(kAutoOpen, cfg.autoOpen ? 1 : 0);
    cvars->SetInteger(kActorModels, cfg.actorModels ? 1 : 0);
    cvars->SetInteger(kRememberSession, cfg.rememberSession ? 1 : 0);
    cvars->SetString(kLastRomhackPath, cfg.lastRomhackPath.c_str());
    cvars->SetInteger(kLastMap, cfg.lastMapId);
    cvars->SetFloat(kCamX, cfg.lastEye[0]);
    cvars->SetFloat(kCamY, cfg.lastEye[1]);
    cvars->SetFloat(kCamZ, cfg.lastEye[2]);
    cvars->SetFloat(kCamYaw, cfg.lastYaw);
    cvars->SetFloat(kCamPitch, cfg.lastPitch);
    cvars->Save();
    return true;
}

} // namespace Lightbulb

namespace Lightbulb {
namespace {

bool RunPicker(Ship::FileBrowserRequest request, std::string& outPath) {
    bool picked = false;
    Lighthouse::PickFile(std::move(request), [&](std::optional<std::filesystem::path> chosen) {
        if (chosen.has_value()) {
            outPath = chosen->string();
            picked = true;
        }
    });
    return picked;
}
} // namespace

bool OpenFileDialog(const char* title, const std::vector<Ship::FileFilter>& filters, std::string& outPath) {
    Ship::FileBrowserRequest request;
    request.Title = title;
    request.Filters = filters;
    return RunPicker(std::move(request), outPath);
}

bool SaveFileDialog(const char* title, const std::vector<Ship::FileFilter>& filters, const std::string& defaultName,
                    std::string& outPath) {
    Ship::FileBrowserRequest request;
    request.Title = title;
    request.Filters = filters;
    request.Save = true;
    request.DefaultName = defaultName;
    return RunPicker(std::move(request), outPath);
}

} // namespace Lightbulb
