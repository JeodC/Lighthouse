#include "App.h"

#include <ship/Context.h>

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Lightbulb {
namespace {
#ifdef _WIN32
const char* kGameExecutable = "Lighthouse.exe";
#else
const char* kGameExecutable = "Lighthouse";
#endif
} // namespace

bool LaunchLighthouse(std::string& outError) {
    std::error_code ec;
    std::filesystem::path game = std::filesystem::path(Ship::Context::GetAppBundlePath()) / kGameExecutable;
#ifdef __APPLE__
    if (!std::filesystem::exists(game, ec)) {
        game = std::filesystem::path(Ship::Context::GetAppBundlePath()).parent_path() / "MacOS" / kGameExecutable;
    }
#endif
    if (!std::filesystem::exists(game, ec)) {
        outError = "Lighthouse was not found beside Lightbulb.";
        return false;
    }

#ifdef _WIN32
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring path = game.wstring();
    if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        outError = "Could not start Lighthouse.";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    std::string path = game.string();
    char* args[] = { path.data(), nullptr };

    const pid_t pid = fork();
    if (pid < 0) {
        outError = std::string("Could not start Lighthouse: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        if (fork() == 0) {
            execv(path.c_str(), args);
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return true;
#endif
}

} // namespace Lightbulb
