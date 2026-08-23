#pragma once

#include "O2rImport.h"

#include "ship/window/gui/FileBrowserWindow.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Lightbulb {
enum LayerBit : uint32_t {
    kLayerModels = 1u << 0,
    kLayerSprites = 1u << 1,
    kLayerActors = 1u << 2,
    kLayerEntries = 1u << 3,
    kLayerWarps = 1u << 4,
    kLayerCamMarkers = 1u << 5,
    kLayerEnemies = 1u << 6,
    kLayerPaths = 1u << 7,
    kLayerTriggers = 1u << 8,
    kLayerFlags = 1u << 9,
    kLayerCameras = 1u << 10,
    kLayerRadius = 1u << 11,
    kLayerBoundary = 1u << 12,
    kLayerUnregistered = 1u << 13,
};
struct Config {
    std::string lastO2rPath;
    float cameraSpeed = 40.0f;
    bool actorOverrides = true;
    bool autoPlayLevelMusic = false;
    bool animateObjects = true;
    uint32_t layers = 0xFFFFFFFFu & ~kLayerUnregistered;
};
bool LoadConfig(Config& out);
bool SaveConfig(const Config& cfg);
bool OpenFileDialog(const char* title, const std::vector<Ship::FileFilter>& filters, std::string& outPath);
bool SaveFileDialog(const char* title, const std::vector<Ship::FileFilter>& filters, const std::string& defaultName,
                    std::string& outPath);
} // namespace Lightbulb

class App {
public:
    App();
    ~App();

    void DrawFrame();
    bool RenderLevelGameFrame();
    void EnforceDefaultLayout();
    bool ShouldClose() const {
        return mShouldClose;
    }
    void RequestClose() {
        mShouldClose = true;
    }

private:
    void DrawMenuBar();
    void DrawToolbar();
    void DrawLevelsPanel();
    void DrawObjectsTab();
    void DrawCamerasTab();
    void DrawPathsTab();
    void DrawLayersPanel();
    void DrawSelectionProperties();
    void DrawLevelProperties();
    void DrawPropertiesPanel();
    void DrawStatusBar();
    void DrawModelViewer();
    void DrawSpriteViewer();
    void DrawSoundViewer();
    void DrawMusicViewer();
    void ResumeLevelMusic();
    void DrawReloadOffer();
    void DrawPreferences();
    void DrawCredits();

    struct O2rView;
    void DrawO2rBrowser(const char* idPrefix, const char* assetDir, O2rView& v);
    void OpenO2r();
    void OpenRomhackO2r();
    bool OpenO2rPath(const std::string& path);
    bool OpenRomhackPath(const std::string& path);
    void SaveSettings();
    void ResetLoadedScene();

    bool mShouldClose = false;
    bool mLayoutInitialized = false;
    bool mFreshLayout = false;

    bool mO2rLoaded = false;
    std::string mO2rPath;
    std::string mAdjacentO2rPath;
    std::string mRomhackPath;

    Lightbulb::Config mConfig;
    std::string mStatus = "No bk.o2r loaded. File > Open bk.o2r... to begin.";

    bool mShowModels = false;
    bool mShowSprites = false;
    bool mShowSounds = false;
    bool mShowMusic = false;
    bool mMusicPanelOpen = false;
    bool mShowPreferences = false;
    bool mShowCredits = false;

    struct O2rView {
        std::vector<std::string> paths;
        int sel = 0;
        char filter[64] = { 0 };
        float yaw = 30.0f, pitch = 20.0f, dist = 1500.0f;
        float center[3] = { 0.0f, 0.0f, 0.0f };
        float listW = 280.0f;
        bool reframe = true;
        std::vector<std::string> animPaths;
        int animSel = -1;
        char animFilter[64] = { 0 };
        bool animShowAll = false;
        bool animPlay = true;
        bool animLoop = true;
        float animProgress = 0.0f;
        float animDuration = 2.0f;
        double animLastTime = 0.0;
    };
    O2rView mObjView;

    struct SpriteView {
        std::vector<std::string> paths;
        int sel = 0;
        char filter[64] = { 0 };
        float listW = 280.0f;
        bool play = true;
        int manualFrame = -1;
        double animTime = 0.0;
        double lastTime = 0.0;
        std::vector<void*> thumbTex;
        int thumbSel = -1;
        int thumbPx = 0;
        bool showChunks = false;
        std::vector<void*> chunkTex;
        int chunkKey = -1;
    };
    SpriteView mSpriteView;

    struct SoundView {
        std::vector<std::string> paths;
        int sel = -1;
        char filter[64] = { 0 };
        float listW = 280.0f;
        bool showInstruments = false;
        bool pitched = true;
        bool autoPlay = true;
        std::string lastExport;
        bool lastExportOk = false;
        Lightbulb::O2rSound cur;
        int curSel = -2;
        bool curOk = false;
    };
    SoundView mSoundView;

    struct MusicView {
        std::vector<std::string> paths;
        int sel = -1;
        char filter[64] = { 0 };
        float listW = 260.0f;
        bool autoPlay = true;
        int playing = -1;
    };
    MusicView mMusicView;

    struct LevelEntry {
        std::string name;
        uint16_t mapId = 0;
        std::vector<std::string> chunks;
        std::string setupPath;
    };
    struct LevelScene {
        std::vector<LevelEntry> entries;
        int sel = -1;
        float eye[3] = { 0.0f, 0.0f, 0.0f };
        float yaw = 0.0f, pitch = 0.0f;
        bool framed = false;
        bool looking = false;
    };
    LevelScene mLevelScene;
    Lightbulb::SetupScene mSetup;
    int mPropSel = -1;
    struct PickTarget {
        int sel = -1;
        float min[3] = { 0, 0, 0 };
        float max[3] = { 0, 0, 0 };
    };
    std::vector<PickTarget> mPickTargets;

    std::map<uint32_t, std::string> mModelIndex;
    std::map<uint32_t, std::string> mSpriteIndex;
};
