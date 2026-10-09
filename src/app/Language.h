#pragma once
// Which interface language is in use: the built-in catalogs (assets/lang)
// and the packs the user put in <config>/lang. See core/I18n.h.

#include "core/I18n.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bz::app {

struct Settings;

/// Where the user puts language packs (created when shown).
std::filesystem::path LanguagePackDir();
/// The page about language packs, opened in the browser on request.
inline constexpr const char* kLanguagePacksUrl = "https://github.com/MikeVerx/breezecad/tree/next/translations";

std::vector<i18n::Language> AvailableLanguages();

/// Switches to the language in `settings` (empty: as the system; a missing
/// pack: Russian). Returns an error text in Russian, or empty.
std::string ApplyLanguage(const Settings& settings);

} // namespace bz::app
