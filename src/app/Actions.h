#pragma once

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bz::app {

/// A user command that can be run from the ribbon, menus, the command
/// search (Ctrl+K) and a keyboard shortcut. One definition, many entry points.
struct Action {
    std::string           id;          ///< "file.save"
    std::string           label;       ///< "Сохранить"
    std::string           icon;        ///< icon name, may be empty
    ImGuiKeyChord         chord = 0;   ///< 0 = no shortcut
    std::string           tooltip;     ///< longer explanation, optional
    std::function<bool()> enabled;     ///< null = always enabled
    std::function<void()> run;
    ImGuiKeyChord         defaultChord = 0;   ///< as shipped (set by Add), for Сбросить

    bool IsEnabled() const { return !enabled || enabled(); }
};

class ActionRegistry {
public:
    void Add(Action action);
    const Action* Find(std::string_view id) const;
    const std::vector<Action>& All() const { return myActions; }

    /// Runs an action if it exists and is enabled. Returns true if it ran.
    bool Run(std::string_view id) const;

    /// Runs the actions whose shortcuts are in `pressed` (chords captured
    /// at key-press time, see Application). Ignored while a text field has
    /// the keyboard or a popup is open.
    void HandleShortcuts(const std::vector<ImGuiKeyChord>& pressed) const;

    /// "Ctrl+Shift+S" for a chord (empty for none).
    static std::string ChordText(ImGuiKeyChord chord);
    /// The chord ChordText wrote; 0 for empty or unknown text.
    static ImGuiKeyChord ParseChord(std::string_view text);

    /// Changes a shortcut. Another action that had the same chord loses it;
    /// its id is returned (empty when none).
    std::string SetChord(std::string_view id, ImGuiKeyChord chord);
    /// Every shortcut as shipped.
    void ResetChords();

    /// Actions whose label contains `query` (case-insensitive, Cyrillic too).
    std::vector<const Action*> Search(std::string_view query) const;

private:
    std::vector<Action> myActions;
};

} // namespace bz::app
