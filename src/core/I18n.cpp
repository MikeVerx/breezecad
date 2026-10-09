#include "core/I18n.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bz::i18n {
namespace {

struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

using StringMap = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;

/// A key with "{}" holes ("Удалить {}"): the text between the holes.
struct Pattern {
    std::vector<std::string> parts;   ///< holes + 1 pieces
    std::string              text;    ///< the translation, with as many "{}"
};

struct Catalog {
    std::string          code = "ru";
    StringMap            strings;
    std::vector<Pattern> patterns;
    StringMap            labels;    ///< "Готово##id" composed once, so the pointer stays valid
    StringMap            dynamic;   ///< texts made at run time, matched against the patterns
};

std::vector<std::string> SplitHoles(std::string_view key) {
    std::vector<std::string> parts;
    std::size_t from = 0;
    for (std::size_t at = key.find("{}"); at != std::string_view::npos; at = key.find("{}", from)) {
        parts.emplace_back(key.substr(from, at - from));
        from = at + 2;
    }
    parts.emplace_back(key.substr(from));
    return parts;
}

/// The holes of `parts` filled to make `text`, or false.
bool Match(const std::vector<std::string>& parts, std::string_view text, std::vector<std::string_view>& holes) {
    holes.clear();
    if (!text.starts_with(parts.front())) return false;
    std::size_t pos = parts.front().size();
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string& piece = parts[i];
        std::size_t at;
        if (i + 1 == parts.size()) {   // the last piece ends the text
            if (!text.ends_with(piece) || text.size() - piece.size() < pos) return false;
            at = text.size() - piece.size();
        } else {
            at = piece.empty() ? std::string_view::npos : text.find(piece, pos);
            if (at == std::string_view::npos) return false;
        }
        holes.push_back(text.substr(pos, at - pos));
        pos = at + piece.size();
    }
    return true;
}

const std::string* Exact(const Catalog& c, std::string_view text) {
    const auto it = c.strings.find(text);
    return it == c.strings.end() ? nullptr : &it->second;
}

Catalog& Active() {
    static Catalog catalog;
    return catalog;
}

/// The head of a catalog file: code and name, without reading the strings.
std::optional<Language> ReadHead(const std::filesystem::path& file, bool builtIn) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    const auto code = j.find("code");
    const auto name = j.find("name");
    if (code == j.end() || !code->is_string() || name == j.end() || !name->is_string()) return std::nullopt;
    Language l{code->get<std::string>(), name->get<std::string>(), file, builtIn};
    if (l.code.empty() || l.code == "ru") return std::nullopt;   // Russian is the source, not a catalog
    return l;
}

} // namespace

std::vector<Language> Available(const std::filesystem::path& builtInDir, const std::filesystem::path& packDir) {
    std::vector<Language> out{{"ru", "Русский", {}, true}};
    const auto scan = [&](const std::filesystem::path& dir, bool builtIn) {
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json") files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            auto l = ReadHead(file, builtIn);
            if (!l) continue;
            const auto same = std::find_if(out.begin(), out.end(), [&](const Language& o) { return o.code == l->code; });
            if (same != out.end())
                *same = std::move(*l);   // a pack replaces the built-in catalog of its language
            else
                out.push_back(std::move(*l));
        }
    };
    scan(builtInDir, true);
    scan(packDir, false);
    return out;
}

Status Use(const Language& language) {
    if (language.file.empty()) {
        Active() = Catalog{};
        return Status::Ok();
    }
    std::ifstream in(language.file, std::ios::binary);
    if (!in) return Error{"Не удалось открыть файл языка"};
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    const auto strings = j.is_object() ? j.find("strings") : j.end();
    if (j.is_discarded() || strings == j.end() || !strings->is_object()) return Error{"Файл языка повреждён"};
    Catalog next;
    next.code = language.code;
    for (const auto& [key, value] : strings->items()) {
        if (!value.is_string() || value.get_ref<const std::string&>().empty()) continue;
        if (key.find("{}") != std::string::npos) {
            Pattern p{SplitHoles(key), value.get<std::string>()};
            const bool hasText = std::any_of(p.parts.begin(), p.parts.end(), [](const std::string& x) { return !x.empty(); });
            if (hasText) next.patterns.push_back(std::move(p));
        }
        next.strings.emplace(key, value.get<std::string>());
    }
    // Longer patterns first: "Не удалось открыть «{}»: {}" before "{}: {}".
    std::stable_sort(next.patterns.begin(), next.patterns.end(), [](const Pattern& a, const Pattern& b) {
        std::size_t la = 0, lb = 0;
        for (const auto& x : a.parts) la += x.size();
        for (const auto& x : b.parts) lb += x.size();
        return la > lb;
    });
    Active() = std::move(next);
    return Status::Ok();
}

const std::string& Current() { return Active().code; }

std::string SystemDefault() {
    std::string tag;
#ifdef _WIN32
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
        for (const wchar_t* p = name; *p && *p != L'-'; ++p) tag += static_cast<char>(*p);
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* v = std::getenv(var); v && *v) {
            tag = v;
            break;
        }
    tag = tag.substr(0, tag.find_first_of("_.@"));
#endif
    for (const char* code : {"ru", "uk", "be", "kk"})
        if (tag == code) return "ru";
    // An unset or "C" locale on Linux is usually a minimal system, not a choice.
    if (tag.empty() || tag == "C" || tag == "POSIX") return "ru";
    return "en";
}

const char* Tr(const char* russian) {
    Catalog& c = Active();
    if (c.strings.empty() || !russian) return russian;
    const std::string_view text(russian);
    if (const std::string* t = Exact(c, text)) return t->c_str();
    // "Готово##save": the label is translated, the ImGui id after "##" kept.
    const std::size_t id = text.find("##");
    if (id == std::string_view::npos || id == 0) return russian;
    if (const auto it = c.labels.find(text); it != c.labels.end()) return it->second.c_str();
    const std::string* label = Exact(c, text.substr(0, id));
    if (!label) return russian;
    return c.labels.emplace(std::string(text), *label + std::string(text.substr(id))).first->second.c_str();
}

std::string Tr(std::string_view russian) {
    Catalog& c = Active();
    if (c.strings.empty()) return std::string(russian);
    if (const std::string* t = Exact(c, russian)) return *t;
    if (c.patterns.empty()) return std::string(russian);
    if (const auto it = c.dynamic.find(russian); it != c.dynamic.end()) return it->second;
    std::string out(russian);
    std::vector<std::string_view> holes;
    for (const Pattern& p : c.patterns) {
        if (!Match(p.parts, russian, holes)) continue;
        // The holes may hold Russian too (an error inside a message).
        out.clear();
        std::size_t from = 0, hole = 0;
        for (std::size_t at = p.text.find("{}"); at != std::string::npos; at = p.text.find("{}", from)) {
            out.append(p.text, from, at - from);
            if (hole < holes.size()) {
                const std::string* inner = Exact(c, holes[hole]);
                out += inner ? std::string_view(*inner) : holes[hole];
            }
            ++hole;
            from = at + 2;
        }
        out.append(p.text, from);
        break;
    }
    if (c.dynamic.size() > 4096) c.dynamic.clear();   // texts with numbers in them: do not grow forever
    c.dynamic.emplace(std::string(russian), out);
    return out;
}

} // namespace bz::i18n
