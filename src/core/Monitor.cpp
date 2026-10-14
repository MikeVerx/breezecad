#include "core/Monitor.h"

#include "core/Log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#define BZ_WRITE _write
#define BZ_FILENO _fileno
#else
#include <unistd.h>
#define BZ_WRITE ::write
#define BZ_FILENO fileno
#endif

namespace bz::mon {
namespace {

using Clock = std::chrono::steady_clock;

struct State {
    std::mutex              mutex;
    std::FILE*              file = nullptr;
    Clock::time_point       start = Clock::now();
    std::atomic<bool>       enabled{false};
    std::atomic<long long>  beatNs{0};        // since start
    std::atomic<int>        waiting{0};
    double                  hangMs = 2000.0;
    double                  lastMemoryMb = -1.0;
    double                  lastMemoryAt = -1.0;
    std::thread             watchdog;
    std::condition_variable wake;
    bool                    stopping = false;
    char                    action[256] = {};   // copy for crash handlers
    std::filesystem::path   dumpDir;
};

State& S() {
    static State state;   // process-wide, like the log
    return state;
}

double Seconds() { return std::chrono::duration<double>(Clock::now() - S().start).count(); }

long long Nanos() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - S().start).count(); }

void Escape(std::string& out, std::string_view text) {
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<int>(c));
                else out += c;
        }
    }
}

std::string Quote(std::string_view text) {
    std::string out = "\"";
    Escape(out, text);
    out += '"';
    return out;
}

/// `fields` is the rest of the object: `"ms":12.3,"name":"…"`.
void Emit(std::string_view kind, const std::string& fields) {
    State& s = S();
    const std::string line = std::format("{{\"t\":{:.3f},\"k\":\"{}\"{}{}}}\n", Seconds(), kind, fields.empty() ? "" : ",", fields);
    std::lock_guard lock(s.mutex);
    if (s.file) std::fputs(line.c_str(), s.file);
}

void Watch() {
    State& s = S();
    bool   inHang = false;
    double hangFrom = 0;
    std::unique_lock lock(s.mutex);
    while (!s.stopping) {
        s.wake.wait_for(lock, std::chrono::milliseconds(100));
        if (s.stopping) break;
        const double now = Seconds();
        const double beat = static_cast<double>(s.beatNs.load()) * 1e-9;
        const bool   blocked = s.waiting.load() == 0 && (now - beat) * 1000.0 > s.hangMs;
        if (blocked && !inHang) {
            inHang = true;
            hangFrom = beat;
            std::fputs(std::format("{{\"t\":{:.3f},\"k\":\"hang\",\"since\":{:.3f},\"act\":{}}}\n", now, beat, Quote(s.action)).c_str(),
                       s.file);
        } else if (!blocked && inHang && (now - beat) * 1000.0 < s.hangMs) {
            inHang = false;
            std::fputs(std::format("{{\"t\":{:.3f},\"k\":\"hang_end\",\"since\":{:.3f},\"ms\":{:.0f}}}\n", now, hangFrom,
                                   (beat - hangFrom) * 1000.0)
                           .c_str(),
                       s.file);
        }
        std::fflush(s.file);   // the tail must be on disk if the process dies
    }
}

} // namespace

void StartFromEnvironment() {
    State& s = S();
#if defined(_WIN32)
    // The wide variable: the path may have letters outside the ANSI code page.
    const wchar_t* path = _wgetenv(L"BZ_MONITOR");
    if (!path || !*path) return;
    s.file = _wfopen(path, L"wb");
#else
    const char* path = std::getenv("BZ_MONITOR");
    if (!path || !*path) return;
    s.file = std::fopen(path, "wb");
#endif
    if (!s.file) return;
    if (const char* hang = std::getenv("BZ_MONITOR_HANG_MS")) s.hangMs = std::max(100.0, std::atof(hang));
    s.dumpDir = std::filesystem::path(path).parent_path();
    s.enabled = true;
    s.beatNs = Nanos();
    Emit("start", std::format("\"hang_ms\":{:.0f}", s.hangMs));
    s.watchdog = std::thread(Watch);
}

void Stop(int exitCode) {
    State& s = S();
    if (!s.enabled) return;
    Emit("exit", std::format("\"code\":{}", exitCode));
    {
        std::lock_guard lock(s.mutex);
        s.stopping = true;
    }
    s.wake.notify_all();
    if (s.watchdog.joinable()) s.watchdog.join();
    std::lock_guard lock(s.mutex);
    s.enabled = false;
    std::fclose(s.file);
    s.file = nullptr;
}

bool Enabled() { return S().enabled.load(std::memory_order_relaxed); }

void Action(std::string_view source, std::string_view name) {
    State& s = S();
    log::Debug("Action: {} {}", source, name);
    {
        std::lock_guard lock(s.mutex);
        std::snprintf(s.action, sizeof s.action, "%.*s: %.*s", static_cast<int>(source.size()), source.data(),
                      static_cast<int>(name.size()), name.data());
    }
    if (Enabled()) Emit("act", std::format("\"src\":{},\"name\":{}", Quote(source), Quote(name)));
}

std::string_view LastAction() { return S().action; }

void Beat() { S().beatNs.store(Nanos(), std::memory_order_relaxed); }

double SecondsSinceBeat() {
    const State& s = S();
    if (s.waiting.load() > 0) return 0.0;
    return static_cast<double>(Nanos() - s.beatNs.load(std::memory_order_relaxed)) * 1e-9;
}

void Frame(double ms, const std::vector<prof::Part>& parts) {
    if (!Enabled()) return;
    std::string fields = std::format("\"ms\":{:.2f}", ms);
    // Parts only for frames long enough to matter (a 30 fps frame).
    if (ms > 33.0 && !parts.empty()) {
        fields += ",\"parts\":{";
        bool first = true;
        for (const prof::Part& p : parts) {
            if (p.ms < 0.5) continue;
            fields += std::format("{}{}:{:.1f}", first ? "" : ",", Quote(p.name), p.ms);
            first = false;
        }
        fields += '}';
    }
    Emit("frame", fields);
}

void Memory(double mb) {
    State& s = S();
    if (!Enabled()) return;
    const double now = Seconds();
    if (std::abs(mb - s.lastMemoryMb) < 2.0 && now - s.lastMemoryAt < 5.0) return;
    s.lastMemoryMb = mb;
    s.lastMemoryAt = now;
    Emit("mem", std::format("\"mb\":{:.1f}", mb));
}

void Timed(std::string_view kind, std::string_view detail, double ms) {
    if (!Enabled()) return;
    Emit("timed", std::format("\"what\":{},\"detail\":{},\"ms\":{:.1f}", Quote(kind), Quote(detail), ms));
}

void Redraws(int frames, double seconds, std::string_view why) {
    if (!Enabled()) return;
    Emit("redraw", std::format("\"frames\":{},\"s\":{:.1f},\"why\":{}", frames, seconds, Quote(why)));
}

void LogLine(bool error, std::string_view message) {
    if (!Enabled()) return;
    Emit("log", std::format("\"lvl\":\"{}\",\"msg\":{}", error ? "error" : "warning", Quote(message)));
}

void CrashNote(const char* text) {
    State& s = S();
    if (!s.enabled.load() || !s.file) return;
    std::fflush(s.file);   // may lose a line if the crash was inside a write
    char line[640];
    const int n = std::snprintf(line, sizeof line, "{\"t\":%.3f,\"k\":\"crash\",\"what\":\"%s\",\"act\":\"", Seconds(), text);
    std::string_view act = s.action;
    int m = n;
    for (const char c : act) {   // escaping by hand: no allocation here
        if (m > static_cast<int>(sizeof line) - 8) break;
        if (c == '"' || c == '\\') line[m++] = '\\';
        line[m++] = (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
    }
    line[m++] = '"';
    line[m++] = '}';
    line[m++] = '\n';
    (void)!BZ_WRITE(BZ_FILENO(s.file), line, static_cast<unsigned>(m));
}

const std::filesystem::path& DumpDir() {
    static const std::filesystem::path none;
    return S().enabled ? S().dumpDir : none;
}

Waiting::Waiting() { ++S().waiting; }
Waiting::~Waiting() {
    Beat();
    --S().waiting;
}

} // namespace bz::mon
