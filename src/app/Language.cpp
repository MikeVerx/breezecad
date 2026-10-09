#include "app/Language.h"

#include "app/Settings.h"
#include "core/Log.h"
#include "platform/Paths.h"

#include <algorithm>

namespace bz::app {

std::filesystem::path LanguagePackDir() { return platform::ConfigDir() / "lang"; }

std::vector<i18n::Language> AvailableLanguages() {
    return i18n::Available(platform::AssetsDir() / "lang", LanguagePackDir());
}

std::string ApplyLanguage(const Settings& settings) {
    const std::string wanted = settings.language.empty() ? i18n::SystemDefault() : settings.language;
    const std::vector<i18n::Language> all = AvailableLanguages();
    const auto it = std::find_if(all.begin(), all.end(), [&](const i18n::Language& l) { return l.code == wanted; });
    // No catalog for it (yet): the source language.
    const i18n::Language& chosen = it != all.end() ? *it : all.front();
    if (const Status s = i18n::Use(chosen); !s) {
        log::Warn("Language '{}' not loaded: {}", chosen.code, s.GetError().message);
        (void)i18n::Use(all.front());
        return s.GetError().message;
    }
    log::Info("Interface language: {}", chosen.code);
    return {};
}

} // namespace bz::app
