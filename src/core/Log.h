#pragma once

#include <filesystem>
#include <format>
#include <string_view>

namespace bz::log {

enum class Level : unsigned char { Debug, Info, Warning, Error };

/// Starts writing the log to `file` in addition to stderr. The previous
/// log is kept as `<file>.old`. Logs never leave the computer.
void OpenFile(const std::filesystem::path& file);

/// Path of the current log file, empty when logging only to stderr.
std::filesystem::path FilePath();

void Write(Level level, std::string_view message);

template <class... Args>
void Debug(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Warning, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace bz::log
