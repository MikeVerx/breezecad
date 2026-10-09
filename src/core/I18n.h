#pragma once
// Interface language.
//
// The program is written in Russian: the Russian text of a string is its key.
// A catalog maps those keys to another language; a string missing from the
// catalog stays Russian, so a half-done translation still works.
//
//   Tr("Готово")                         text for the current language
//   Trf("Выбрано рёбер: {}", count)      the same, then std::format
//   N_("Готово")                         only marks text for the extractor
//                                        (static tables); Tr() it when shown
//
// Catalog file (assets/lang/en.json, or a pack in <config>/lang/):
//   { "code": "en", "name": "English", "strings": { "Готово": "Done", ... } }
//
// Not thread safe: call from the interface thread.

#include "core/Expected.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#define N_(text) text

namespace bz::i18n {

struct Language {
    std::string           code;      ///< "en", "de"; "ru" is the source language
    std::string           name;      ///< in that language: "English", "Deutsch"
    std::filesystem::path file;      ///< empty for Russian
    bool                  builtIn = false;
};

/// Russian, the catalogs in `builtInDir` and the packs in `packDir`
/// (a pack with the code of a built-in language replaces it).
std::vector<Language> Available(const std::filesystem::path& builtInDir, const std::filesystem::path& packDir);

/// Switches to `language` (Russian when its file is empty).
Status Use(const Language& language);
/// The code of the language in use ("ru" at start).
const std::string& Current();

/// The language to start with when the user has not chosen one: Russian for
/// a Russian, Ukrainian, Belarusian or Kazakh system, English otherwise.
std::string SystemDefault();

/// A literal or a static table entry: only an exact catalog entry (and
/// "text##id" for ImGui labels). The pointer stays valid until Use().
const char* Tr(const char* russian);
/// Text made at run time (errors, messages with names in them): also matched
/// against catalog keys with "{}" holes, e.g. "Удалить {}" -> "Delete {}".
std::string Tr(std::string_view russian);

/// A translation with broken placeholders falls back to the Russian pattern:
/// a typo in a language pack must not take the program down.
template <class... Args>
std::string Trf(const char* russianPattern, Args&&... args) {
    const char* pattern = Tr(russianPattern);
    try {
        return std::vformat(pattern, std::make_format_args(args...));
    } catch (const std::format_error&) {
        return std::vformat(russianPattern, std::make_format_args(args...));
    }
}

} // namespace bz::i18n

namespace bz {
using i18n::Tr;
using i18n::Trf;
} // namespace bz
