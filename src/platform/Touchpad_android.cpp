// Touchpad gestures on Android: none from the system here — touches come
// through the window as pointers (app/mobile). The queue stays, so the app's
// code is the same everywhere.
#include "platform/Touchpad.h"

#include <utility>

namespace bz::platform {
namespace {
std::vector<TouchEvent> gEvents;
}

void detail::PushTouch(const TouchEvent& event) {
    if (gEvents.size() < 4096) gEvents.push_back(event);
}
std::vector<TouchEvent> TakeTouchEvents() { return std::exchange(gEvents, {}); }
void SetSmoothScrollIsTouchpad(bool) {}
void StartTouchpad(GLFWwindow*) {}
void StopTouchpad() {}
TouchSupport TouchpadSupport() { return {}; }

} // namespace bz::platform
