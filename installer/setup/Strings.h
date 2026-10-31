#pragma once
// The installer's words in 16 languages (installer/setup/strings/*.json,
// packed into the .exe as resources). A key missing in a language falls
// back to English, then Russian; the installer never shows a bare key.

#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bz::setup {

struct LanguageInfo {
    const char* code;
    const char* name;   ///< in the language itself
};
/// Every language the installer speaks, in the order they are listed.
std::span<const LanguageInfo> Languages();

/// The language of Windows' interface, if the installer speaks it; else English.
std::string SystemLanguage();

void               UseLanguage(const std::string& code);
const std::string& CurrentLanguage();

/// The text of `key` in the current language (UTF-8; valid until UseLanguage).
const char* Tr(std::string_view key);

template <class... Args>
std::string Trf(std::string_view key, Args&&... args) {
    const char* pattern = Tr(key);
    try {
        return std::vformat(pattern, std::make_format_args(args...));
    } catch (const std::format_error&) {
        return pattern;
    }
}

/// "12,4 МБ" / "1.2 GB" in the current language's words and decimal mark.
std::string Size(std::uint64_t bytes);

/// A resource packed into the .exe (RCDATA by name), empty when missing.
std::span<const std::uint8_t> Resource(const wchar_t* name);

} // namespace bz::setup
