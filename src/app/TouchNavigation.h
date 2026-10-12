#pragma once
// What touchpad gestures do in the view: moving two fingers (with or
// without a modifier key), pinching, turning two fingers; and which command
// the extra gestures run (two-finger double tap, force click). The same on
// every system; a gesture the system does not deliver is greyed out in the
// settings (platform/Touchpad.h).

#include "app/MouseNavigation.h"

#include <string>
#include <string_view>
#include <vector>

namespace bz::app {

/// What moving two fingers does.
enum class TouchMove : unsigned char { None, Pan, Orbit, Zoom };
/// What turning two fingers does.
enum class TouchTurn : unsigned char { None, Roll, Orbit };

struct TouchNavigation {
    TouchMove   twoFingers = TouchMove::Pan;
    TouchMove   withShift = TouchMove::Orbit;
    TouchMove   withCtrl = TouchMove::Zoom;    ///< Cmd on macOS
    TouchMove   withAlt = TouchMove::None;     ///< Option on macOS
    bool        pinchZooms = true;
    TouchTurn   turn = TouchTurn::Roll;
    std::string doubleTap = "view.fit";        ///< command id (TouchCommands); empty: nothing
    std::string forceClick;                    ///< the same
    double      speed = 1.0;                   ///< 0.5…2
    bool        invert = false;                ///< the view moves against the fingers
    bool        smoothIsTouchpad = true;       ///< Windows, Linux: smooth scrolling is taken for fingers

    /// What two fingers do with these modifier keys held (as ModifierChoice).
    TouchMove MoveWith(ModifierChoice modifier) const;
    bool operator==(const TouchNavigation&) const = default;
};

/// The commands a single gesture can run, in the order the settings list
/// them; "" is nothing and kTouchMenu opens the view's context menu.
inline constexpr std::string_view kTouchMenu = "view.menu";
const std::vector<std::string_view>& TouchCommands();

/// The modifier key as this system names it: Ctrl is Cmd on macOS, Alt is
/// Option.
const char* ModifierName(ModifierChoice m);

} // namespace bz::app
