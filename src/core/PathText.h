#pragma once
// Conversions between file paths and UTF-8 text.
//
// All text in BreezeCAD is UTF-8 (UI, JSON, logs). On Windows a path is
// wide internally, and path::string() converts to the ANSI code page: it
// garbles or throws on Cyrillic names like "Деталь1.bzc". Always convert
// through these two functions instead.

#include <filesystem>
#include <string>
#include <string_view>

namespace bz {

/// The path as UTF-8 text, for showing, logging and storing in JSON.
inline std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/// A path from UTF-8 text (JSON, file dialogs, command line on POSIX).
inline std::filesystem::path PathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace bz
