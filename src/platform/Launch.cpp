#include "platform/Launch.h"

#if defined(__ANDROID__)
#include "platform/Android.h"
#elif defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace bz::platform {
namespace {

#if defined(__ANDROID__)
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
#if defined(__ANDROID__)
    return android::OpenUrl(url);
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
#if defined(__ANDROID__)
    return false;   // no file manager to show the app's own folder in
#elif defined(_WIN32)
    return Open(folder.wstring());
#else
    return Open(folder.string());
#endif
}

} // namespace bz::platform
