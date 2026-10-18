#include "app/BetaTesting.h"

#include "core/Folders.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "core/Version.h"
#include "kernel/Model.h"
#include "platform/Paths.h"
#include "platform/SystemInfo.h"
#include "platform/Window.h"

#include "ui/Gl.h"
#include <imgui.h>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace bz::app::beta {
namespace {

constexpr const char* kFlag = "beta-tester.txt";
constexpr const char* kRunning = "running";       ///< present while the session runs; left by a crash
constexpr const char* kSeen = "reported";         ///< the tester packed it or said no
constexpr std::size_t kKeepSessions = 30;
constexpr int kHangMs = 2000;

std::filesystem::path gSession;

/// Local time as "2026-10-11_16-28-52".
std::string Stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d_%H-%M-%S", &local);
    return text;
}

#if !defined(__APPLE__)
bool Writable(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path probe = dir / ".probe";
    { std::ofstream(probe) << "x"; }
    const bool ok = std::filesystem::exists(probe, ec);
    std::filesystem::remove(probe, ec);
    return ok;
}
#endif

std::filesystem::path SessionsDir() { return ReportsDir() / "sessions"; }

void WriteJson(const std::filesystem::path& file, const nlohmann::json& j) { std::ofstream(file, std::ios::binary) << j.dump(1, '\t'); }

nlohmann::json ReadJson(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    return j.is_discarded() ? nlohmann::json::object() : j;
}

void SetEnv(const char* name, const std::filesystem::path& value) {
#if defined(_WIN32)
    const std::wstring wide(name, name + std::char_traits<char>::length(name));
    _wputenv_s(wide.c_str(), value.wstring().c_str());
#else
    setenv(name, value.string().c_str(), 1);
#endif
}

/// Old sessions go, the newest kKeepSessions stay (the zips are the tester's).
void Prune() {
    std::error_code ec;
    std::vector<std::filesystem::path> all;
    for (const auto& e : FolderEntries(SessionsDir()))   // never throws (B-20)
        if (IsFolder(e)) all.push_back(e.path());
    std::ranges::sort(all);   // names are dates
    for (std::size_t i = 0; i + kKeepSessions < all.size(); ++i) std::filesystem::remove_all(all[i], ec);
}

/// Where the flag is, if anywhere. In a macOS bundle anything but code in
/// Contents/MacOS breaks the signature, so there it sits in Resources.
std::filesystem::path FlagFile() {
    std::error_code ec;
#if defined(__APPLE__)
    const std::filesystem::path bundled = platform::ExecutableDir() / ".." / "Resources" / kFlag;
    if (std::filesystem::exists(bundled, ec)) return bundled;
#endif
    const std::filesystem::path beside = platform::ExecutableDir() / kFlag;
    return std::filesystem::exists(beside, ec) ? beside : std::filesystem::path{};
}

#if defined(__APPLE__)
/// macOS writes its own crash report (.ips, with the stack of every thread)
/// to ~/Library/Logs/DiagnosticReports; the ones of this program written
/// since the session started go into the zip.
void CopySystemCrashReports(const std::filesystem::path& sessionDir) {
    std::error_code ec;
    const auto started = std::filesystem::last_write_time(sessionDir / "session.json", ec);
    if (ec) return;
    const char* home = std::getenv("HOME");
    if (!home) return;
    for (const auto& e : FolderEntries(std::filesystem::path(home) / "Library" / "Logs" / "DiagnosticReports")) {
        const std::string name = ToUtf8(e.path().filename());
        if (!IsFile(e) || !name.starts_with("breezecad") || e.last_write_time(ec) < started) continue;
        std::filesystem::copy_file(e.path(), sessionDir / name, std::filesystem::copy_options::skip_existing, ec);
    }
}
#endif

} // namespace

bool Enabled() {
    static const bool enabled = !FlagFile().empty();
    return enabled;
}

std::filesystem::path ReportsDir() {
    static const std::filesystem::path dir = [] {
#if defined(__APPLE__)
        return platform::ConfigDir() / u8"Отчёты для разработчика";
#else
        // The flag file may name the folder ("reports=..\\Отчёты для разработчика"),
        // relative to the program: the testers' archive keeps it next to the
        // instructions, above the folder with the program.
        std::filesystem::path chosen = platform::ExecutableDir() / u8"Отчёты для разработчика";
        std::ifstream in(FlagFile(), std::ios::binary);
        for (std::string line; std::getline(in, line);) {
            if (!line.starts_with("reports=")) continue;
            std::string value = line.substr(8);
            while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
            std::ranges::replace(value, '\\', '/');
            if (!value.empty())
                chosen = (platform::ExecutableDir() / std::filesystem::path(std::u8string(value.begin(), value.end()))).lexically_normal();
        }
        return Writable(chosen) ? chosen : platform::DocumentsDir() / u8"BreezeCAD — отчёты тестера";
#endif
    }();
    return dir;
}

void BeginSession() {
    if (!Enabled()) return;
    std::error_code ec;
    const std::string stamp = Stamp();
    std::filesystem::path dir = SessionsDir() / stamp;
    for (int i = 2; std::filesystem::exists(dir, ec); ++i) dir = SessionsDir() / std::format("{}-{}", stamp, i);
    std::filesystem::create_directories(dir, ec);
    if (ec) return;
    gSession = dir;
    { std::ofstream(dir / kRunning) << "running"; }
    SetEnv("BZ_MONITOR", dir / "events.jsonl");
    SetEnv("BZ_MONITOR_HANG_MS", std::to_string(kHangMs));
    WriteJson(dir / "session.json", {{"started", Stamp()},
                                     {"version", VersionLine()},
                                     {"binary", ToUtf8(platform::ExecutableDir())},
                                     {"hang_ms", kHangMs},
                                     {"exit_code", nullptr}});
    Prune();
}

const std::filesystem::path& SessionDir() { return gSession; }

void WriteSystemInfo(const platform::Window& window, const std::string& language) {
    if (gSession.empty()) return;
    nlohmann::json j;
    j["program"] = {{"version", VersionLine()},
                    {"geometry kernel", kernel::GeometryKernelVersion()},
                    {"dear imgui", IMGUI_VERSION},
                    {"interface language", language},
#if defined(__clang__)
                    {"compiler", std::format("clang {}.{}", __clang_major__, __clang_minor__)},
#elif defined(__GNUC__)
                    {"compiler", std::format("gcc {}.{}", __GNUC__, __GNUC_MINOR__)},
#endif
                    {"folder", ToUtf8(platform::ExecutableDir())}};
    j["graphics"] = {{"opengl", window.OpenGLVersion()}, {"vendor", window.OpenGLVendor()}, {"renderer", window.OpenGLRenderer()}};
    nlohmann::json computer = nlohmann::json::object();
    for (const platform::SystemFact& f : platform::DescribeSystem(ReportsDir())) computer[f.key] = f.value;
    j["computer"] = computer;
    WriteJson(gSession / "system.json", j);
    log::Info("Testers' build: reports in {}", ToUtf8(gSession));
}

void EndSession(int exitCode) {
    if (gSession.empty()) return;
    nlohmann::json j = ReadJson(gSession / "session.json");
    j["exit_code"] = exitCode;
    j["ended"] = Stamp();
    WriteJson(gSession / "session.json", j);
    std::error_code ec;
    std::filesystem::remove(gSession / kRunning, ec);
}

std::vector<PastSession> UnreportedCrashes() {
    std::vector<PastSession> out;
    if (!Enabled()) return out;
    std::error_code ec;
    for (const auto& e : FolderEntries(SessionsDir())) {
        const std::filesystem::path dir = e.path();
        if (!IsFolder(e) || dir == gSession) continue;
        if (!std::filesystem::exists(dir / kRunning, ec) || std::filesystem::exists(dir / kSeen, ec)) continue;
        std::string name = ToUtf8(dir.filename());   // 2026-10-11_16-28-52
        std::string started = name.size() >= 16 ? std::format("{} {}:{}", name.substr(0, 10), name.substr(11, 2), name.substr(14, 2)) : name;
        out.push_back({dir, started});
    }
    std::ranges::sort(out, [](const PastSession& a, const PastSession& b) { return a.dir > b.dir; });
    return out;
}

void Dismiss(const std::filesystem::path& sessionDir) { std::ofstream(sessionDir / kSeen) << "seen"; }

Expected<std::filesystem::path> Pack(const std::filesystem::path& sessionDir, const std::string& description, bool crash) {
    std::error_code ec;
#if defined(__APPLE__)
    if (crash) CopySystemCrashReports(sessionDir);
#endif
    {
        std::ofstream out(sessionDir / "описание.txt", std::ios::binary);
        out << (crash ? "Программа закрылась с ошибкой.\n\n" : "Отчёт по просьбе тестера (без вылета).\n\n") << description << "\n";
    }
    const std::string name = ToUtf8(sessionDir.filename());
    const std::filesystem::path zipPath = ReportsDir() / (u8"BreezeCAD-отчёт-" + sessionDir.filename().u8string() + u8".zip");
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) return Error{"Не удалось подготовить архив"};
    bool ok = true;
    for (const auto& e : FolderEntries(sessionDir)) {
        if (!IsFile(e)) continue;
        const std::string file = ToUtf8(e.path().filename());
        if (file == kRunning || file == kSeen) continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        ok = ok && mz_zip_writer_add_mem(&zip, (name + "/" + file).c_str(), bytes.data(), bytes.size(), MZ_BEST_COMPRESSION);
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    mz_zip_writer_end(&zip);
    if (!ok) {
        mz_free(buffer);
        return Error{"Не удалось собрать архив с отчётом"};
    }
    std::ofstream out(zipPath, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    mz_free(buffer);
    if (!out) return Error{"Не удалось сохранить архив с отчётом"};
    Dismiss(sessionDir);
    return zipPath;
}

} // namespace bz::app::beta
