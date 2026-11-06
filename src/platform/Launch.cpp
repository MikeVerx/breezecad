#include "platform/Launch.h"

#if defined(BZ_MOBILE)
#include "platform/Mobile.h"
#elif defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace bz::platform {
namespace {

#if defined(BZ_MOBILE)
#elif defined(_WIN32)
bool Open(const std::wstring& target) {
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}
#else
/// Runs the system opener detached, so the program does not wait for it.
bool Open(const std::string& target) {
#ifdef __APPLE__
    const char* opener = "open";
#else
    const char* opener = "xdg-open";
#endif
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        // Double fork: the opener is not our child, no zombie to collect.
        if (fork() == 0) {
            execlp(opener, opener, target.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

} // namespace

bool OpenUrl(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;   // only web pages, never local commands
#if defined(BZ_MOBILE)
    return mobile::OpenUrl(url);
#elif defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), n);
    return Open(wide);
#else
    return Open(url);
#endif
}

bool OpenFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) return false;
#if defined(BZ_MOBILE)
    return false;   // no file manager to show the app's own folder in
#elif defined(_WIN32)
    return Open(folder.wstring());
#else
    return Open(folder.string());
#endif
}

bool ShowInFolder(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return OpenFolder(file.parent_path());
#if defined(BZ_MOBILE)
    return false;
#elif defined(_WIN32)
    // Explorer with the file selected.
    const std::wstring args = L"/select,\"" + file.wstring() + L"\"";
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif defined(__APPLE__)
    // Finder with the file selected (open -R).
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        if (fork() == 0) {
            execlp("open", "open", "-R", file.string().c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    // File managers differ in selecting a file; its folder opens everywhere.
    return OpenFolder(file.parent_path());
#endif
}

} // namespace bz::platform
