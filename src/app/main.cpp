// BreezeCAD — entry point.
//
// Usage: breezecad [file.bzc ...]
// Test hooks:
//   BZ_SCREENSHOT=out.ppm[:frame]  saves one frame and exits;
//   BZ_SHOT_DIR=dir                saves a frame whenever dir/request appears.

#include "app/Application.h"

#include <cstdlib>
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
