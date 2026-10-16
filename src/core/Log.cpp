#include "core/Log.h"

#include "core/Monitor.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

#if defined(__ANDROID__)
#include <android/log.h>   // stderr goes nowhere on Android: logcat instead
#endif

namespace bz::log {
namespace {

struct Sink {
    std::mutex            mutex;
    std::ofstream         file;
    std::filesystem::path path;
};

Sink& GetSink() {
    static Sink sink;   // process-wide logger; the only global state in core
    return sink;
}

std::string_view Tag(Level level) {
    switch (level) {
        case Level::Debug:   return "DEBUG";
        case Level::Info:    return "INFO ";
        case Level::Warning: return "WARN ";
        case Level::Error:   return "ERROR";
    }
    return "?";
}

} // namespace

void OpenFile(const std::filesystem::path& file) {
    Sink& sink = GetSink();
    std::lock_guard lock(sink.mutex);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (std::filesystem::exists(file, ec)) {
        std::filesystem::rename(file, std::filesystem::path(file).concat(".old"), ec);
    }
    sink.file.open(file, std::ios::out | std::ios::trunc);
    sink.path = sink.file.is_open() ? file : std::filesystem::path{};
}

std::filesystem::path FilePath() {
    Sink& sink = GetSink();
    std::lock_guard lock(sink.mutex);
    return sink.path;
}

void Write(Level level, std::string_view message) {
    const auto now  = std::chrono::system_clock::now();
    const auto time = std::chrono::floor<std::chrono::milliseconds>(now);
    const std::string line = std::format("{:%H:%M:%S} {} {}\n", time, Tag(level), message);

    Sink& sink = GetSink();
    std::unique_lock lock(sink.mutex);
#if defined(__ANDROID__)
    const int priority = level == Level::Error ? ANDROID_LOG_ERROR : level == Level::Warning ? ANDROID_LOG_WARN
                       : level == Level::Info  ? ANDROID_LOG_INFO  : ANDROID_LOG_DEBUG;
    __android_log_write(priority, "BreezeCAD", line.c_str());
#else
    std::fputs(line.c_str(), stderr);
#endif
    if (sink.file.is_open()) {
        sink.file << line;
        sink.file.flush();   // keep the tail on disk if the process dies
    }
    lock.unlock();
    if (level >= Level::Warning) mon::LogLine(level == Level::Error, message);
}

} // namespace bz::log
