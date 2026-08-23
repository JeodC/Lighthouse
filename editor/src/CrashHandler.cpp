#include "CrashHandler.h"

#ifdef _WIN32
#include <crtdbg.h>
#include <spdlog/spdlog.h>
#endif

#ifdef _WIN32
#include <windows.h>

#include <dbghelp.h>
#ifdef _WIN32
static int __cdecl crtReportHook(int type, char* msg, int* ret) {
    if (type != _CRT_ASSERT && type != _CRT_ERROR) {
        return FALSE;
    }
    static thread_local bool inHook = false;
    if (inHook) {
        if (ret) {
            *ret = 0;
        }
        return TRUE;
    }
    inHook = true;

    SPDLOG_ERROR("CRT assertion: {}", msg ? msg : "(none)");
    void* frames[48];
    USHORT frameCount = CaptureStackBackTrace(1, 48, frames, nullptr);
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
    SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    for (USHORT frame = 0; frame < frameCount; ++frame) {
        DWORD64 disp = 0;
        DWORD lineDisp = 0;
        IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
        const DWORD64 addr = (DWORD64)frames[frame];
        if (SymFromAddr(proc, addr, &disp, sym)) {
            if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
                SPDLOG_ERROR("    {} ({}:{})", sym->Name, line.FileName, line.LineNumber);
            } else {
                SPDLOG_ERROR("    {}", sym->Name);
            }
        } else {
            SPDLOG_ERROR("    0x{:016X}", addr);
        }
    }

    inHook = false;
    if (ret) {
        *ret = 0;
    }
    return TRUE;
}
#endif

static LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    SPDLOG_CRITICAL("=== CRASH: exception 0x{:08X} at {} ===", (uint32_t)info->ExceptionRecord->ExceptionCode,
                    (const void*)info->ExceptionRecord->ExceptionAddress);
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 stackFrame = {};
    stackFrame.AddrPC.Offset = ctx.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = ctx.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = ctx.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
    char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
    SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    for (int frame = 0; frame < 40; ++frame) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &stackFrame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            stackFrame.AddrPC.Offset == 0) {
            break;
        }
        DWORD64 disp = 0;
        DWORD lineDisp = 0;
        IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
        if (SymFromAddr(proc, stackFrame.AddrPC.Offset, &disp, sym)) {
            if (SymGetLineFromAddr64(proc, stackFrame.AddrPC.Offset, &lineDisp, &line)) {
                SPDLOG_CRITICAL("  #{:02d} {} +0x{:x}  ({}:{})", frame, sym->Name, (unsigned long long)disp,
                                line.FileName, (unsigned long)line.LineNumber);
            } else {
                SPDLOG_CRITICAL("  #{:02d} {} +0x{:x}", frame, sym->Name, (unsigned long long)disp);
            }
        } else {
            SPDLOG_CRITICAL("  #{:02d} 0x{:x}", frame, (unsigned long long)stackFrame.AddrPC.Offset);
        }
    }
    spdlog::default_logger()->flush();
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

namespace Lightbulb {
void InstallCrashHandler() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashFilter);
    _CrtSetReportHook(crtReportHook);
#endif
}
} // namespace Lightbulb
