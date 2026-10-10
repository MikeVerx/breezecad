// BreezeCAD — entry point.
//
// Usage: breezecad [file.bzc ...]
// Test hooks:
//   BZ_SCREENSHOT=out.ppm[:frame]  saves one frame and exits;
//   BZ_SHOT_DIR=dir                saves a frame whenever dir/request appears.

#include "app/Application.h"
#include "core/Log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
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
LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    bz::log::Error("Crash: exception 0x{:08x} at {}", info->ExceptionRecord->ExceptionCode,
                   Where(info->ExceptionRecord->ExceptionAddress));
    void* frames[32];
    const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) bz::log::Error("  #{} {}", i, Where(frames[i]));
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace
#endif

namespace {

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
#if defined(_WIN32)
    SetUnhandledExceptionFilter(OnCrash);
#endif
    bz::app::Application app;
    if (!app.Init(CommandLineFiles(argc, argv))) return 1;
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
    return app.Run();
}
