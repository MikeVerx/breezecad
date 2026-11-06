#include "app/mobile/TouchText.h"

#include <array>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace bz::app::mobile {
namespace {

void ReplaceAll(std::string& s, std::string_view from, std::string_view to) {
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

/// A piece that is about a key: it starts with the key's name.
bool AboutKey(std::string_view piece) {
    while (!piece.empty() && (piece.front() == ' ' || piece.front() == '(')) piece.remove_prefix(1);
    static constexpr std::array<std::string_view, 14> kKeys{"Enter", "Esc", "Ctrl", "Shift", "Alt", "Tab", "Delete", "Del",
                                                            "Backspace", "Пробел", "ПКМ", "СКМ", "Колесо", "колесо"};
    for (std::string_view k : kKeys)
        if (piece.starts_with(k)) {
            const std::string_view rest = piece.substr(k.size());
            // "Esc" but not "Escape-like" words: the key's name ends there.
            if (rest.empty() || !std::isalpha(static_cast<unsigned char>(rest.front()))) return true;
        }
    // F1…F12
    return piece.size() >= 2 && piece[0] == 'F' && std::isdigit(static_cast<unsigned char>(piece[1]));
}

} // namespace

std::string TouchHint(std::string hint) {
    // "(Ctrl+N)" and the like: shortcuts in brackets.
    for (std::size_t at = hint.find('('); at != std::string::npos; at = hint.find('(', at + 1)) {
        const std::size_t end = hint.find(')', at);
        if (end == std::string::npos) break;
        const std::string_view inside(hint.data() + at + 1, end - at - 1);
        if (inside.starts_with("Ctrl+") || inside.starts_with("Shift+") || inside.starts_with("Alt+")) {
            const std::size_t start = at > 0 && hint[at - 1] == ' ' ? at - 1 : at;
            hint.erase(start, end + 1 - start);
            at = start == 0 ? 0 : start - 1;
        }
    }
    // Pieces between ". " , ", " and "; ": those about keys go.
    std::vector<std::pair<std::string, std::string>> pieces;   // text, the delimiter after it
    std::size_t from = 0;
    while (from <= hint.size()) {
        std::size_t best = std::string::npos, length = 0;
        for (std::string_view d : {std::string_view(". "), std::string_view(", "), std::string_view("; ")}) {
            const std::size_t at = hint.find(d, from);
            if (at < best) best = at, length = d.size();
        }
        if (best == std::string::npos) {
            pieces.push_back({hint.substr(from), ""});
            break;
        }
        pieces.push_back({hint.substr(from, best - from), hint.substr(best, length)});
        from = best + length;
    }
    std::string out;
    std::string pendingDelimiter;
    for (auto& [text, delimiter] : pieces) {
        // "(Esc)" and the like inside a piece.
        for (std::string_view k : {"(Esc)", "(Enter)", "(Tab)"}) ReplaceAll(text, std::string(" ") + std::string(k), ""), ReplaceAll(text, k, "");
        if (AboutKey(text) || text.find_first_not_of(" .") == std::string::npos) {
            // Its sentence end stays when it ended one ("… грань. Esc — отмена" → "… грань.").
            if (delimiter == ". " || (text.ends_with('.') && !out.empty())) pendingDelimiter = ". ";
            continue;
        }
        if (!out.empty()) out += pendingDelimiter.empty() ? ", " : pendingDelimiter;
        out += text;
        pendingDelimiter = delimiter;
    }
    if (pendingDelimiter == ". " && !out.empty() && !out.ends_with('.')) out += '.';
    // Clicks are taps.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kWords{{
        {"Двойной щелчок", "Двойное касание"}, {"двойной щелчок", "двойное касание"},
        {"Щёлкните", "Коснитесь"}, {"щёлкните", "коснитесь"}, {"Щелкните", "Коснитесь"}, {"щелкните", "коснитесь"},
        {"Щелчки", "Касания"}, {"щелчки", "касания"}, {"Щелчок", "Касание"}, {"щелчок", "касание"},
        {"щелчком", "касанием"}, {"щелчка", "касания"}, {"щелчку", "касанию"}, {"Наведите мышь", "Наведите"},
    }};
    for (const auto& [a, b] : kWords) ReplaceAll(out, a, b);
    return out;
}

} // namespace bz::app::mobile
