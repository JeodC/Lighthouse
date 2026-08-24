#include "LaunchArgs.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <libultraship/libultraship.h>
#include <ship/utils/StringHelper.h>

#include "UI/LighthouseModMenuWindow.h"
#include "UI/cvar_prefixes.h"

namespace Lighthouse {

namespace {

// Romhack requested via `-hack <name>`; empty when the flag was not passed.
std::string sLaunchHack;

// Lightbulb, installed beside the game on every platform.
#ifdef _WIN32
const char* kEditorExecutable = "Lightbulb.exe";
#else
const char* kEditorExecutable = "Lightbulb";
#endif

// Persisted mod selection captured before the override, so it can be restored.
std::string sSavedEnabledMods;
std::string sSavedDisabledMods;
bool sLaunchHackApplied = false;

// Everything here runs before context->InitLogging(), so diagnostics are buffered
// and emitted by FlushLaunchLog() once the logger is live.
std::string sLaunchLog;
bool sLaunchLogIsError = false;

void SetLaunchLog(bool isError, std::string message) {
    sLaunchLog = std::move(message);
    sLaunchLogIsError = isError;
}

} // namespace

bool IsLaunchHackFlag(const std::string& arg, std::string& outInlineValue, bool& outTakesValue) {
    static const char* kFlags[] = { "-hack", "--hack", "-romhack", "--romhack" };
    for (const char* flag : kFlags) {
        if (arg == flag) {
            outTakesValue = true;
            return true;
        }
        const std::string prefix = std::string(flag) + "=";
        if (arg.rfind(prefix, 0) == 0) {
            outInlineValue = arg.substr(prefix.size());
            outTakesValue = false;
            return true;
        }
    }
    return false;
}

void ParseLaunchArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        std::string inlineValue;
        bool takesValue = false;
        if (!IsLaunchHackFlag(arg, inlineValue, takesValue)) {
            continue;
        }
        if (takesValue) {
            if (i + 1 < argc) {
                sLaunchHack = argv[++i];
            } else {
                SetLaunchLog(true, "[Launch] " + arg + " requires a romhack name");
            }
        } else {
            sLaunchHack = inlineValue;
        }
    }
}

void ApplyLaunchHack() {
    if (sLaunchHack.empty()) {
        return;
    }

    // Only strip the extension when it is actually ".o2r", so a hack whose stem
    // contains a dot ("my.hack") still resolves when named without the suffix.
    const std::filesystem::path requested(sLaunchHack);
    const std::string wanted = StringHelper::IEquals(requested.extension().string(), ".o2r")
                                   ? requested.stem().generic_string()
                                   : requested.filename().generic_string();

    const std::string modsPath = Ship::Context::GetPathRelativeToAppDirectory("mods");
    if (modsPath.empty() || !std::filesystem::is_directory(modsPath)) {
        SetLaunchLog(true, "[Launch] -hack '" + sLaunchHack + "': no mods directory to search");
        return;
    }

    std::string resolved;
    std::string available;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             modsPath, std::filesystem::directory_options::follow_directory_symlink, ec)) {
        if (entry.is_directory() || !StringHelper::IEquals(entry.path().extension().string(), ".o2r")) {
            continue;
        }
        const std::string stem = entry.path().stem().generic_string();
        if (!available.empty()) {
            available += ", ";
        }
        available += stem;
        if (StringHelper::IEquals(stem, wanted)) {
            resolved = stem;
        }
    }

    if (resolved.empty()) {
        SetLaunchLog(true, "[Launch] -hack '" + sLaunchHack + "': no matching .o2r under mods/. Available: " +
                               (available.empty() ? std::string("(none)") : available));
        return;
    }

    // Snapshot before overriding so RestoreModSelectionAfterLaunchHack() can put
    // the user's own selection back once the archives are loaded.
    sSavedEnabledMods = CVarGetString(CVAR_SETTING("EnabledMods"), "");
    sSavedDisabledMods = CVarGetString(CVAR_SETTING("DisabledMods"), "");
    sLaunchHackApplied = true;

    // Validates that the match actually carries an aGameConfig, and disables any
    // other overlay so the boot-time conflict check does not quarantine them.
    SetLaunchLog(false, "[Launch] -hack '" + sLaunchHack + "': enabling romhack overlay '" + resolved + "'");
    SetSoleEnabledRomhack(resolved);
}

void RestoreModSelectionAfterLaunchHack() {
    if (!sLaunchHackApplied) {
        return;
    }
    CVarSetString(CVAR_SETTING("EnabledMods"), sSavedEnabledMods.c_str());
    CVarSetString(CVAR_SETTING("DisabledMods"), sSavedDisabledMods.c_str());
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void FlushLaunchLog() {
    if (sLaunchLog.empty()) {
        return;
    }
    if (sLaunchLogIsError) {
        SPDLOG_ERROR("{}", sLaunchLog);
    } else {
        SPDLOG_INFO("{}", sLaunchLog);
    }
    sLaunchLog.clear();
}

bool LaunchEditorIfRequested(int argc, char* argv[]) {
    bool requested = false;
    for (int i = 1; i < argc && !requested; i++) {
        const std::string arg = argv[i];
        requested = (arg == "-editor" || arg == "--editor");
    }
    if (!requested) {
        return false;
    }

    const std::filesystem::path editor = std::filesystem::path(Ship::Context::GetAppBundlePath()) / kEditorExecutable;
    std::error_code ec;
    if (!std::filesystem::exists(editor, ec)) {
        SetLaunchLog(true, "[Launch] --editor: Lightbulb was not found beside Lighthouse");
        return false;
    }

#ifdef _WIN32
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring path = editor.wstring();
    if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        SetLaunchLog(true, "[Launch] --editor: could not start Lightbulb");
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    std::string path = editor.string();
    char* args[] = { path.data(), nullptr };
    execv(path.c_str(), args);
    SetLaunchLog(true, std::string("[Launch] --editor: could not start Lightbulb: ") + strerror(errno));
    return false;
#endif
}

} // namespace Lighthouse
