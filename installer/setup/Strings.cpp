#include "Strings.h"

#include "Install.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cstdlib>
#include "Resources.h"
#endif

#include <nlohmann/json.hpp>

#include <map>

namespace bz::setup {
namespace {

constexpr LanguageInfo kLanguages[] = {
    {"ru", "Русский"},  {"en", "English"},  {"uk", "Українська"}, {"be", "Беларуская"}, {"kk", "Қазақша"}, {"de", "Deutsch"},
    {"fr", "Français"}, {"es", "Español"},  {"it", "Italiano"},   {"pt", "Português"},  {"pl", "Polski"},  {"cs", "Čeština"},
    {"tr", "Türkçe"},   {"zh", "简体中文"}, {"ja", "日本語"},     {"ko", "한국어"},
};

using Table = std::map<std::string, std::string, std::less<>>;

Table Load(const std::string& code) {
    Table t;
    const std::wstring name = L"STR_" + Wide(code);
    const auto bytes = Resource(name.c_str());
    if (bytes.empty()) return t;
    try {
        const auto j = nlohmann::json::parse(bytes.begin(), bytes.end());
        for (const auto& [k, v] : j.items())
            if (v.is_string()) t[k] = v.get<std::string>();
    } catch (...) {
    }
    return t;
}

struct State {
    std::string code = "en";
    Table       current, english, russian;
};
State& S() {
    static State s;
    return s;
}

} // namespace

std::span<const LanguageInfo> Languages() { return kLanguages; }

std::string SystemLanguage() {
#ifndef _WIN32
    std::string tag;
    for (const char* var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(var); v && *v) {
            tag = v;
            break;
        }
    tag = tag.substr(0, tag.find_first_of("_.@:"));
    for (const LanguageInfo& l : kLanguages)
        if (tag == l.code) return tag;
    return "en";
#else
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    // The interface language, not the region's formats: a Russian Windows set
    // to German number formats still speaks Russian.
    ULONG count = 0, size = LOCALE_NAME_MAX_LENGTH;
    std::wstring tag;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, name, &size) && count > 0) tag = name;
    else if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) tag = name;
    const std::string code = Utf8(tag.substr(0, tag.find(L'-')));
    for (const LanguageInfo& l : kLanguages)
        if (code == l.code) return code;
    return "en";
#endif
}

void UseLanguage(const std::string& code) {
    State& s = S();
    if (s.english.empty()) s.english = Load("en");
    if (s.russian.empty()) s.russian = Load("ru");
    s.code = code;
    s.current = code == "en" ? s.english : code == "ru" ? s.russian : Load(code);
}

const std::string& CurrentLanguage() { return S().code; }

const char* Tr(std::string_view key) {
    const State& s = S();
    for (const Table* t : {&s.current, &s.english, &s.russian})
        if (const auto it = t->find(key); it != t->end()) return it->second.c_str();
    static thread_local std::string missing;
    missing = std::string(key);
    return missing.c_str();
}

std::string Size(std::uint64_t bytes) {
    const bool comma = std::string_view("ru uk be kk de fr es it pt pl cs tr").find(CurrentLanguage()) != std::string_view::npos;
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::string out = mb >= 1024.0 ? std::format("{:.1f} {}", mb / 1024.0, Tr("gb"))
                      : mb < 10    ? std::format("{:.1f} {}", mb, Tr("mb"))
                                   : std::format("{:.0f} {}", mb, Tr("mb"));
    if (comma)
        for (char& c : out)
            if (c == '.') c = ',';
    return out;
}

std::span<const std::uint8_t> Resource(const wchar_t* name) {
#ifndef _WIN32
    return EmbeddedResource(Utf8(name));
#else
    HRSRC found = FindResourceW(nullptr, name, MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!found) return {};
    HGLOBAL loaded = LoadResource(nullptr, found);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data) return {};
    return {static_cast<const std::uint8_t*>(data), SizeofResource(nullptr, found)};
#endif
}

} // namespace bz::setup
