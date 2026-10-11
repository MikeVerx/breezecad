// BreezeCAD — entry point.
//
// Usage: breezecad [file.bzc ...]
// Test hooks:
//   BZ_SCREENSHOT=out.ppm[:frame]  saves one frame and exits;
//   BZ_SHOT_DIR=dir                saves a frame whenever dir/request appears;
//   BZ_MONITOR=events.jsonl        monitoring for tools/monitor (core/Monitor.h).

#include "app/Application.h"
#include "app/BetaTesting.h"
#include "core/Log.h"
#include "core/Monitor.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>

// Laptops with two GPUs (NVIDIA Optimus, AMD switchable graphics) run a
// program on the integrated one unless the executable asks otherwise. These
// exported names are what the drivers look for.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

/// "module+0xoffset" for an address in a loaded exe or DLL.
std::string Where(const void* address) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, name, MAX_PATH)) {
        const char* base = std::strrchr(name, '\\');
        return std::format("{}+0x{:x}", base ? base + 1 : name,
                           reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module));
    }
    return std::format("0x{:x}", reinterpret_cast<std::uintptr_t>(address));
}

/// A crash leaves its trace in the log (the log never leaves the computer;
/// the user can send it with a bug report). Unsaved work is in the autosave.
/// Under the monitor (tools/monitor): a minidump next to its report, to
/// open in a debugger. dbghelp is loaded here, not linked.
void WriteMinidump(EXCEPTION_POINTERS* info) {
    const std::filesystem::path& dir = bz::mon::DumpDir();
    if (dir.empty()) return;
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) return;
    using WriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                    PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    const auto write = reinterpret_cast<WriteDump>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
    if (!write) return;
    const std::filesystem::path file = dir / L"breezecad.dmp";
    HANDLE out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    write(GetCurrentProcess(), GetCurrentProcessId(), out,
          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &exception, nullptr, nullptr);
    CloseHandle(out);
    bz::log::Error("Crash dump: {}", file.string());
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    bz::mon::CrashNote("exception");
    bz::log::Error("Crash: exception 0x{:08x} at {}", info->ExceptionRecord->ExceptionCode,
                   Where(info->ExceptionRecord->ExceptionAddress));
    void* frames[32];
    const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) bz::log::Error("  #{} {}", i, Where(frames[i]));
    bz::log::Error("Last action: {}", bz::mon::LastAction());
    WriteMinidump(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace
#elif defined(__linux__) || defined(__APPLE__)
#include <csignal>
#include <execinfo.h>
#include <unistd.h>

namespace {

void Say(const char* text) { (void)!::write(2, text, std::strlen(text)); }

/// A crash leaves its trace on stderr (the monitor and a terminal see it):
/// only calls that are safe in a signal handler, then the default action
/// (a core file if the system keeps them).
void OnSignal(int signal) {
    const char* name = signal == SIGSEGV ? "SIGSEGV" : signal == SIGABRT ? "SIGABRT" : signal == SIGFPE ? "SIGFPE"
                     : signal == SIGBUS  ? "SIGBUS"  : signal == SIGILL  ? "SIGILL"  : "signal";
    bz::mon::CrashNote(name);
    Say("Crash: ");
    Say(name);
    Say("\nLast action: ");
    Say(bz::mon::LastAction().data());
    Say("\nStack:\n");
    void* frames[64];
    backtrace_symbols_fd(frames, backtrace(frames, 64), 2);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void CatchCrashes() {
    void* warmUp[1];
    backtrace(warmUp, 1);   // loads what backtrace needs now, not in the handler
    // Its own stack, so that a stack overflow is reported too.
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof altStack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_handler = OnSignal;
    sa.sa_flags = static_cast<int>(SA_ONSTACK | SA_RESETHAND);
    for (const int s : {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL}) sigaction(s, &sa, nullptr);
}

} // namespace
#endif

namespace {

/// An exception nobody caught: say which before the program ends.
[[noreturn]] void OnTerminate() {
    std::string what = "unknown";
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& x) {
            what = x.what();
        } catch (...) {
        }
    }
    bz::log::Error("Crash: uncaught exception: {} (last action: {})", what, bz::mon::LastAction());
    bz::mon::CrashNote("uncaught exception");
    std::abort();
}

std::vector<std::filesystem::path> CommandLineFiles(int argc, char** argv) {
    std::vector<std::filesystem::path> files;
#if defined(_WIN32)
    // argv is in the ANSI code page on Windows; take the real Unicode one.
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) files.emplace_back(wide[i]);
    LocalFree(wide);
    (void)argc;
    (void)argv;
#else
    for (int i = 1; i < argc; ++i) files.emplace_back(argv[i]);
#endif
    return files;
}

} // namespace

int main(int argc, char** argv) {
    bz::app::beta::BeginSession();   // the testers' build: monitoring into its session folder
    bz::mon::StartFromEnvironment();
    std::set_terminate(OnTerminate);
#if defined(_WIN32)
    SetUnhandledExceptionFilter(OnCrash);
#elif defined(__linux__) || defined(__APPLE__)
    CatchCrashes();
#endif
    bz::app::Application app;
    if (!app.Init(CommandLineFiles(argc, argv))) {
        bz::mon::Stop(1);
        bz::app::beta::EndSession(1);
        return 1;
    }
    if (const char* shot = std::getenv("BZ_SCREENSHOT")) {
        std::string spec = shot;
        int frame = 20;
        if (const auto colon = spec.rfind(':'); colon != std::string::npos && colon > 1) {
            frame = std::atoi(spec.c_str() + colon + 1);
            spec.resize(colon);
        }
        app.ScreenshotAndQuit(spec, frame);
    }
    if (const char* dir = std::getenv("BZ_SHOT_DIR")) app.EnableShotRequests(dir);
    const int code = app.Run();
    bz::mon::Stop(code);
    bz::app::beta::EndSession(code);
    return code;
}
