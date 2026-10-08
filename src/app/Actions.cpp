#include "app/Actions.h"

#include <algorithm>
#include <cctype>

namespace bz::app {
namespace {

/// Lower-cases ASCII and the Russian alphabet in a UTF-8 string.
std::string Lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
        } else if (c == 0xD0 && i + 1 < text.size()) {
            const auto n = static_cast<unsigned char>(text[i + 1]);
            if (n >= 0x90 && n <= 0x9F) {            // А..П -> а..п
                out += '\xD0';
                out += static_cast<char>(n + 0x20);
            } else if (n >= 0xA0 && n <= 0xAF) {     // Р..Я -> р..я
                out += '\xD1';
                out += static_cast<char>(n - 0x20);
            } else if (n == 0x81) {                  // Ё -> ё
                out += "\xD1\x91";
            } else {
                out += static_cast<char>(c);
                out += static_cast<char>(n);
            }
            ++i;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

} // namespace

void ActionRegistry::Add(Action action) { myActions.push_back(std::move(action)); }

const Action* ActionRegistry::Find(std::string_view id) const {
    for (const auto& a : myActions)
        if (a.id == id) return &a;
    return nullptr;
}

bool ActionRegistry::Run(std::string_view id) const {
    const Action* a = Find(id);
    if (!a || !a->run || !a->IsEnabled()) return false;
    a->run();
    return true;
}

void ActionRegistry::HandleShortcuts(const std::vector<ImGuiKeyChord>& pressed) const {
    // Typing in a field, or a menu/dialog on top: keys belong to them.
    if (ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    // One action per key press: the first enabled one with that chord, so a
    // key can mean different things in different modes (R: rectangle in a
    // sketch, revolve in the model).
    for (const ImGuiKeyChord chord : pressed) {
        for (const auto& a : myActions) {
            if (a.chord != chord || !a.run || !a.IsEnabled()) continue;
            a.run();
            break;
        }
    }
}

std::string ActionRegistry::ChordText(ImGuiKeyChord chord) {
    if (chord == 0) return {};
    std::string out;
    if (chord & ImGuiMod_Ctrl) out += "Ctrl+";
    if (chord & ImGuiMod_Shift) out += "Shift+";
    if (chord & ImGuiMod_Alt) out += "Alt+";
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    switch (key) {
        case ImGuiKey_Delete: out += "Del"; break;
        case ImGuiKey_Escape: out += "Esc"; break;
        case ImGuiKey_Enter:  out += "Enter"; break;
        default:              out += ImGui::GetKeyName(key); break;
    }
    return out;
}

std::vector<const Action*> ActionRegistry::Search(std::string_view query) const {
    const std::string q = Lower(query);
    std::vector<const Action*> found;
    for (const auto& a : myActions) {
        if (!a.run || a.label.empty()) continue;
        if (q.empty() || Lower(a.label).find(q) != std::string::npos) found.push_back(&a);
    }
    // Labels that start with the query first.
    std::stable_sort(found.begin(), found.end(), [&](const Action* x, const Action* y) {
        return (Lower(x->label).rfind(q, 0) == 0) > (Lower(y->label).rfind(q, 0) == 0);
    });
    return found;
}

} // namespace bz::app
