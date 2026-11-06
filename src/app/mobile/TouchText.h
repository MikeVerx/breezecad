#pragma once
// Hints written for a mouse and a keyboard, said again for fingers: on a touch
// screen with no keyboard plugged in "Enter — готово, Esc — отмена" and
// "Ctrl — …" mean nothing, and one taps rather than clicks.

#include <string>

namespace bz::app::mobile {

/// `hint` for a touch screen: keys dropped (Enter, Esc, Ctrl, Shift, Tab…),
/// clicks become taps.
std::string TouchHint(std::string hint);

} // namespace bz::app::mobile
