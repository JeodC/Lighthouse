extern "C" {
#include <libultra/exception.h>
}

#include <stdlib.h>
#include <string.h>

#include "O2rImport.h"
#include <SDL.h>

#include "imgui.h"
#include <fast/Fast3dWindow.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <libultraship/libultraship.h>
#include <ship/Context.h>
#include <ship/config/Config.h>
#include <ship/config/ConsoleVariable.h>
#include <ship/resource/ResourceManager.h>

#include "App.h"

#include <cstdio>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <string>

int main(int, char**) {
    Ship::Context* ctx = Ship::Context::CreateUninitializedInstance("Lightbulb", "lightbulb", "lightbulb.cfg.json");
    ctx->InitLogging();
    ctx->InitCrashHandler();
    ctx->InitConfiguration();
    ctx->InitConsoleVariables();
    ctx->GetConsoleVariables()->SetInteger("gEnableMultiViewports", 0);
    if (auto config = ctx->GetConfig(); config && !config->Contains("Window.Width")) {
        config->SetInt("Window.Width", 1280);
        config->SetInt("Window.Height", 720);
        config->Save();
    }
    ctx->InitResourceManager({ "lighthouse.o2r" }, {}, 1, true);
    if (auto resources = ctx->GetResourceManager(); !resources || !resources->IsLoaded()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Lightbulb", "Couldn't load lighthouse.o2r.", nullptr);
        std::_Exit(1);
    }
    ctx->InitControlDeck(std::make_shared<LUS::ControlDeck>());
    ctx->InitConsole();
    Lightbulb::RegisterBKFactories();

    auto window = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>{});
    ctx->InitWindow(window);
    ctx->InitEventSystem();

    static char imguiIniPath[512];
    std::snprintf(imguiIniPath, sizeof(imguiIniPath), "%s",
                  Ship::Context::GetPathRelativeToAppDirectory("lightbulb.imgui.ini").c_str());
    ImGui::GetIO().IniFilename = imguiIniPath;

    App app;

    while (window->IsRunning() && !app.ShouldClose()) {
        window->HandleEvents();
        if (!window->IsFrameReady()) {
            continue;
        }
        auto gui = window->GetGui();
        window->GetMouseStateManager()->StartFrame();
        gui->StartDraw();
        window->StartFrame();
        app.DrawFrame();
        if (!app.RenderLevelGameFrame()) {
            window->RunGuiOnly();
        }
        gui->EndDraw();
        window->EndFrame();
        app.EnforceDefaultLayout();
    }

    ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    app.SaveSession();
    window->SaveWindowToConfig();
    if (auto config = ctx->GetConfig()) {
        config->Save();
    }
    window->Close();
    spdlog::shutdown();
    std::_Exit(0);
}
