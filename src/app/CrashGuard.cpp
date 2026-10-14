// SPDX-License-Identifier: MPL-2.0
#include "app/CrashGuard.h"

#include "app/BetaTesting.h"
#include "core/Log.h"
#include "core/Monitor.h"
#include "core/PathText.h"
#include "core/Version.h"
#include "platform/Paths.h"
#include "platform/Process.h"
#include "platform/SystemInfo.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace bz::app::crash {
namespace {

// Filled by Prepare and NoteGraphics; read by the crash handlers. Fixed
// buffers: a handler must not allocate.
char gReportPath[4096] = "";
char gHeader[2048] = "";
char gReason[1024] = "";
std::atomic<bool> gShown{false};
#if defined(_WIN32)
wchar_t gReportPathWide[4096] = L"";
wchar_t gProgram[4096] = L"";
wchar_t gCommandLine[8800] = L"";   // CreateProcessW may write into it
#else
char  gProgram[4096] = "";
char  gFlag[] = "--crash-window";
char* gArgv[4] = {gProgram, gFlag, gReportPath, nullptr};
#endif

// Normal code only (Prepare, the hang guard).
std::string           gStarted;
std::string           gSystem;
std::string           gGraphics;
std::filesystem::path gHangReport;
std::filesystem::path gProgramPath;

constexpr int kKeepReports = 20;   ///< crash and hang reports kept in the reports folder (not the testers' sessions)

std::string Stamp(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, format, &local);
    return text;
}

void BuildHeader() {
    std::snprintf(gHeader, sizeof gHeader, "BreezeCAD crash report\nVersion: %s\nSystem: %s\nGraphics: %s\nStarted: %s\n",
                  VersionLine().c_str(), gSystem.empty() ? "?" : gSystem.c_str(), gGraphics.empty() ? "?" : gGraphics.c_str(),
                  gStarted.c_str());
}

/// The oldest reports go: a crash a day for a year would otherwise pile up.
void Prune(const std::filesystem::path& dir) {
    std::error_code ec;
    std::vector<std::filesystem::path> reports;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = ToUtf8(e.path().filename());
        if ((name.starts_with("crash-") || name.starts_with("hang-")) && name.ends_with(".txt")) reports.push_back(e.path());
    }
    if (static_cast<int>(reports.size()) <= kKeepReports) return;
    // The names carry the start time: sorted by name is sorted by age (per kind).
    std::ranges::sort(reports, [](const auto& a, const auto& b) {
        const auto key = [](const std::filesystem::path& p) {
            const std::string n = ToUtf8(p.filename());
            return n.substr(n.find('-') + 1);
        };
        return key(a) < key(b);
    });
    for (std::size_t i = 0; i + kKeepReports < reports.size(); ++i) std::filesystem::remove(reports[i], ec);
}

template <std::size_t N>
void CopyInto(char (&out)[N], const std::string& text) {
    std::snprintf(out, N, "%s", text.c_str());
}

// --- The hang guard ---

struct Guard {
    std::mutex              mutex;
    std::condition_variable wake;
    std::thread             thread;
    bool                    stopping = false;
    double                  seconds = 15.0;
};

Guard& G() {
    static Guard guard;
    return guard;
}

void WriteHangReport(double stalled) {
    std::ofstream out(gHangReport, std::ios::binary | std::ios::trunc);
    std::string header = gHeader;
    if (const auto first = header.find('\n'); first != std::string::npos) header.replace(0, first, "BreezeCAD hang report");
    out << header << "Hang: no response for " << static_cast<int>(stalled) << " s\n"
        << "Last action: " << mon::LastAction() << "\n";
}

void Watch() {
    using Clock = std::chrono::steady_clock;
    Guard& g = G();
    platform::ProcessId window = 0;   ///< the hang window, while it is up
    double quietUntil = 0;            ///< "Wait" chosen: not again before this (seconds since start)
    double pausedAt = -1e9;           ///< the last time this thread itself was held up
    const Clock::time_point start = Clock::now();
    double lastTick = 0;
    std::unique_lock lock(g.mutex);
    while (!g.stopping) {
        g.wake.wait_for(lock, std::chrono::milliseconds(250));
        if (g.stopping) break;
        const double now = std::chrono::duration<double>(Clock::now() - start).count();
        // This thread late too: the computer slept or the process was
        // paused (a debugger, the system napping it). Not a hang of ours.
        if (now - lastTick > 2.0) pausedAt = now;
        lastTick = now;
        const double stalled = mon::SecondsSinceBeat();
        if (window) {
            if (stalled < 1.0) {   // the loop is back
                platform::StopProcess(window);
                window = 0;
                log::Info("Hang: over, the window answers again");
            } else if (!platform::ProcessRunning(window)) {   // closed by the user: "Wait"
                window = 0;
                quietUntil = now + 60.0;
            }
            continue;
        }
        if (stalled < g.seconds || now < quietUntil || now - pausedAt < stalled) continue;
        WriteHangReport(stalled);
        log::Warn("Hang: no response for {:.0f} s (last action: {}); showing the hang window", stalled, mon::LastAction());
        const std::vector<std::string> args = {"--hang-window", std::to_string(platform::CurrentProcessId()), ToUtf8(gHangReport),
                                               std::to_string(static_cast<int>(stalled))};
        if (!platform::StartProcess(gProgramPath, args, &window)) {
            window = 0;
            quietUntil = now + 60.0;
            log::Warn("Hang: the hang window did not start");
        }
    }
    if (window) platform::StopProcess(window);
}

} // namespace

void Prepare() {
    gStarted = Stamp("%Y-%m-%d %H:%M:%S");
    gProgramPath = platform::ExecutablePath();
    const std::filesystem::path& session = beta::SessionDir();
    const std::filesystem::path dir = session.empty() ? platform::ConfigDir() / "reports" : session;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string stamp = Stamp("%Y-%m-%d_%H-%M-%S");
    const std::filesystem::path report = session.empty() ? dir / ("crash-" + stamp + ".txt") : dir / "crash.txt";
    gHangReport = session.empty() ? dir / ("hang-" + stamp + ".txt") : dir / "hang.txt";
    if (session.empty()) Prune(dir);
    CopyInto(gReportPath, ToUtf8(report));
#if defined(_WIN32)
    // Paths in quotes: they cannot hold quotes themselves, and none ends in a backslash.
    const std::wstring wide = report.wstring(), program = gProgramPath.wstring();
    const std::wstring line = L"\"" + program + L"\" --crash-window \"" + wide + L"\"";
    lstrcpynW(gReportPathWide, wide.c_str(), static_cast<int>(std::size(gReportPathWide)));
    lstrcpynW(gProgram, program.c_str(), static_cast<int>(std::size(gProgram)));
    lstrcpynW(gCommandLine, line.c_str(), static_cast<int>(std::size(gCommandLine)));
#else
    CopyInto(gProgram, gProgramPath.string());
#endif
    BuildHeader();
}

void NoteGraphics(const std::string& graphics) {
    gGraphics = graphics;
    for (const platform::SystemFact& f : platform::DescribeSystem(beta::ReportsDir()))
        if (f.key == "os") gSystem = f.value;
    BuildHeader();
}

void NoteReason(const char* reason) { std::snprintf(gReason, sizeof gReason, "Reason: %s\n", reason); }

const char* ReportPath() { return gReportPath; }
#if defined(_WIN32)
const wchar_t* ReportPathWide() { return gReportPathWide; }
#endif
const char* ReportHeader() { return gHeader; }
const char* ReasonLine() { return gReason; }

bool ShowCrashWindow() {
    if (gShown.exchange(true) || !gProgram[0] || !gReportPath[0]) return false;
#if defined(_WIN32)
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(gProgram, gCommandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    // fork and execv only: what a signal handler may call. The window
    // outlives this process, which is about to end.
    const pid_t child = ::fork();
    if (child < 0) return false;
    if (child == 0) {
        ::setsid();
        ::execv(gProgram, gArgv);
        ::_exit(127);
    }
    return true;
#endif
}

void StartHangGuard(double seconds) {
    Guard& g = G();
    if (g.thread.joinable() || gProgramPath.empty()) return;
    g.seconds = seconds;
    g.stopping = false;
    mon::Beat();
    g.thread = std::thread(Watch);
}

void StopHangGuard() {
    Guard& g = G();
    if (!g.thread.joinable()) return;
    {
        std::lock_guard lock(g.mutex);
        g.stopping = true;
    }
    g.wake.notify_all();
    g.thread.join();
}

} // namespace bz::app::crash
