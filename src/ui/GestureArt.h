#pragma once
// Small looping pictures of touchpad gestures for the settings: a trackpad
// and the fingers on it, drawn with the theme's colours (crisp at any scale,
// right in both themes). A key held with the gesture is drawn as a key cap.

#include "ui/Ui.h"

namespace bz::ui {

enum class GestureArt : unsigned char { TwoFingers, Pinch, Rotate, DoubleTap, ForceClick };

/// Draws `art` into the box at `min` of `size` at `time` (seconds; the
/// picture loops). `key`: a key cap beside the trackpad ("Shift"), or null.
/// Disabled: still and greyed (the system does not deliver this gesture).
void DrawGestureArt(const Ui& ui, ImDrawList* dl, ImVec2 min, ImVec2 size, GestureArt art, double time, const char* key = nullptr,
                    bool disabled = false);

} // namespace bz::ui
