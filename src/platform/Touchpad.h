#pragma once
// Touchpad gestures over the window — what GLFW does not report: pinch,
// rotation, smart zoom, force click, and which scrolling comes from fingers
// rather than a mouse wheel. One model for all systems; each system's part
// delivers what it can (TouchpadSupport): macOS (Cocoa) everything, Windows
// (Precision Touchpad) scrolling and pinch, Linux (X11 / XWayland, XInput
// 2.4) scrolling, pinch and rotation. Gestures with three or four fingers
// stay with the system (desktops, app switching) and never come here.
// The system keeps sending scrolling as a mouse wheel as well, so lists and
// panels scroll as before; the view takes these events instead of the wheel.

#include <vector>

struct GLFWwindow;

namespace bz::platform {

enum class TouchGesture : unsigned char {
    Scroll,      ///< two fingers moving
    Pinch,       ///< two fingers apart or together
    Rotate,      ///< two fingers turning
    SmartZoom,   ///< two-finger double tap (macOS)
    ForceClick,  ///< pressing harder after a click (macOS, Force Touch)
};

struct TouchEvent {
    TouchGesture gesture = TouchGesture::Scroll;
    /// Scroll: the way the content follows the fingers, in screen points
    /// (window coordinates): +x right, +y down. The system's scrolling
    /// direction setting ("natural") is already in it.
    float dx = 0.0f, dy = 0.0f;
    /// Pinch: relative change of size (+0.1 = 10 % bigger). Rotate: degrees,
    /// counter-clockwise positive.
    float amount = 0.0f;
    bool  momentum = false;   ///< scroll still gliding after the fingers left (macOS)
};

/// What this system can deliver at all: the settings grey out the rest.
struct TouchSupport {
    bool scroll = false;
    bool pinch = false;
    bool rotate = false;
    bool smartZoom = false;
    bool forceClick = false;
};

/// Starts listening on the window (once, after it exists); StopTouchpad
/// before the window goes.
void StartTouchpad(GLFWwindow* window);
void StopTouchpad();
TouchSupport TouchpadSupport();
/// Windows and Linux tell a touchpad from a wheel partly by guessing: smooth
/// scrolling (not in whole wheel steps) is taken for fingers. A mouse with a
/// free-spinning smooth wheel would then pan instead of zoom: off for it.
void SetSmoothScrollIsTouchpad(bool on);
/// The events since the last call, oldest first.
std::vector<TouchEvent> TakeTouchEvents();

namespace detail {
/// For the systems' parts only.
void PushTouch(const TouchEvent& event);
} // namespace detail

} // namespace bz::platform
