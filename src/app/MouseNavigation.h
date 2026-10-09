#pragma once
// Which mouse gestures turn, move and zoom the view. People keep the habits
// of the CAD they came from, so the sets follow those programs; any gesture
// can also be set by hand.

#include <string>
#include <string_view>
#include <vector>

namespace bz::app {

enum class MouseButtonChoice : unsigned char { None, Right, Middle };
enum class ModifierChoice : unsigned char { None, Shift, Ctrl, Alt };

struct MouseGesture {
    MouseButtonChoice button = MouseButtonChoice::None;
    ModifierChoice    modifier = ModifierChoice::None;
    bool operator==(const MouseGesture&) const = default;
};

struct MouseNavigation {
    MouseGesture orbit{MouseButtonChoice::Right, ModifierChoice::None};
    MouseGesture pan{MouseButtonChoice::Middle, ModifierChoice::None};
    MouseGesture zoom{};                 ///< zoom by dragging up and down (the wheel always zooms)
    bool         invertWheel = false;   ///< wheel forward zooms out
    bool operator==(const MouseNavigation&) const = default;
};

struct MousePreset {
    std::string_view id;
    std::string_view label;
    MouseNavigation  navigation;
};

const std::vector<MousePreset>& MousePresets();
const MousePreset* FindMousePreset(std::string_view id);

/// "ПКМ", "Shift + колесо" — for hints and the settings.
std::string GestureText(const MouseGesture& g);

} // namespace bz::app
