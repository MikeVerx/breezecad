#include "platform/Paths.h"

#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace bz::platform {
namespace {

std::filesystem::path ExecutableDir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
#elif defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) return std::filesystem::current_path();
    return std::filesystem::weakly_canonical(buffer).parent_path();
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : exe.parent_path();
#endif
}

std::filesystem::path EnvPath(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
}

std::filesystem::path Ensure(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace

std::filesystem::path AssetsDir() {
    const std::filesystem::path exe = ExecutableDir();
    for (const auto& candidate : {exe / "assets", exe / ".." / "share" / "breezecad" / "assets",
                                  exe / ".." / "Resources" / "assets"}) {
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec)) return std::filesystem::weakly_canonical(candidate, ec);
    }
    return exe / "assets";
}

std::filesystem::path ConfigDir() {
#if defined(_WIN32)
    PWSTR path = nullptr;
    std::filesystem::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) base = path;
    CoTaskMemFree(path);
    return Ensure(base / "BreezeCAD");
#elif defined(__APPLE__)
    return Ensure(EnvPath("HOME") / "Library" / "Application Support" / "BreezeCAD");
#else
    std::filesystem::path base = EnvPath("XDG_CONFIG_HOME");
    if (base.empty()) base = EnvPath("HOME") / ".config";
    return Ensure(base / "breezecad");
#endif
}

std::filesystem::path AutosaveDir() { return Ensure(ConfigDir() / "autosave"); }

std::filesystem::path DocumentsDir() {
#if defined(_WIN32)
    PWSTR path = nullptr;
    std::filesystem::path docs;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path))) docs = path;
    CoTaskMemFree(path);
    return docs.empty() ? ConfigDir() : docs;
#else
    const std::filesystem::path home = EnvPath("HOME");
    std::error_code ec;
    if (std::filesystem::is_directory(home / "Documents", ec)) return home / "Documents";
    return home.empty() ? std::filesystem::current_path() : home;
#endif
}

} // namespace bz::platform
