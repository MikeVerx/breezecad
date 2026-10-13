// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 MikeVerx
// Touchpad gestures: the queue, and the Windows and Linux (X11) parts.
// The macOS part is Touchpad_mac.mm.
#include "platform/Touchpad.h"

#include "core/Log.h"

#include <chrono>
#include <utility>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <map>
#include <poll.h>
#include <thread>
#include <unistd.h>
#endif

namespace bz::platform {
namespace {

std::vector<TouchEvent> gEvents;   // the main thread only
constexpr std::size_t   kMaxQueued = 4096;

#if !defined(__APPLE__)
bool gSmoothIsTouchpad = true;

/// Smooth scrolling marks a burst as fingers; a whole wheel step inside the
/// burst (it happens now and then) is still fingers, not a wheel.
bool FingersNow(bool smooth) {
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (!gSmoothIsTouchpad) return false;
    if (smooth) last = now;
    return now - last < std::chrono::milliseconds(350);
}
#endif

#if defined(_WIN32)

// Windows sends Precision Touchpad gestures to classic windows as wheel
// messages: two fingers as a smooth wheel (not in whole 120 steps), a pinch
// as a wheel with MK_CONTROL while Ctrl itself is up. Every message goes on
// to the procedure that was there before (ImGui's, then GLFW's).
constexpr float kPointsPerWheelUnit = 0.5f;   // 120 (one wheel step) = 60 px
WNDPROC gPreviousProc = nullptr;   // ImGui's, which hands on to GLFW's
HWND    gWindow = nullptr;

LRESULT CALLBACK OnMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        const int  delta = static_cast<short>(static_cast<unsigned short>((wParam >> 16) & 0xFFFF));
        const bool ctrlFlag = (wParam & MK_CONTROL) != 0;
        const bool ctrlKey = GetKeyState(VK_CONTROL) < 0;
        TouchEvent e;
        if (message == WM_MOUSEWHEEL && ctrlFlag && !ctrlKey) {
            e.gesture = TouchGesture::Pinch;
            e.amount = static_cast<float>(delta) / 1200.0f;
            detail::PushTouch(e);
        } else if (FingersNow(delta % WHEEL_DELTA != 0)) {
            const float points = static_cast<float>(delta) * kPointsPerWheelUnit;
            if (message == WM_MOUSEWHEEL) {
                e.dy = points;    // forward: the content goes down
            } else {
                e.dx = -points;   // right: the content goes left
            }
            detail::PushTouch(e);
        }
    }
    return CallWindowProcW(gPreviousProc, hwnd, message, wParam, lParam);
}

void Pump() {}

#elif defined(__linux__)

// A connection of our own to the X server: GLFW's ignores XInput 2 events
// it does not know. Selecting events is per client, so both get theirs.
// A thread only waits on the socket and wakes the main loop; all Xlib
// calls stay on the main thread.
struct Axis {
    int    number = 0;
    bool   vertical = true;
    double increment = 1.0;   ///< valuator units of one wheel step
};

struct X11 {
    Display*                         display = nullptr;
    ::Window                         window = 0;
    int                              opcode = 0;
    int                              fd = -1;
    bool                             gestures = false;   ///< XInput 2.4: pinch and rotation
    std::map<int, std::vector<Axis>> axes;               ///< device → its scroll valuators
    std::map<int, bool>              touchpad;           ///< device → a touchpad by its driver
    std::map<long long, double>      last;               ///< device and valuator → the last value
    double                           pinchScale = 1.0;
    std::thread                      waker;
    int                              stop[2] = {-1, -1};
    std::atomic<bool>                pending{false};
    std::atomic<bool>                stopping{false};
};
X11  gX;
bool gXFailed = false;

int OnXError(Display*, XErrorEvent*) {
    gXFailed = true;
    return 0;
}

bool MaskBit(const unsigned char* mask, int length, int bit) {
    return bit / 8 < length && (mask[bit / 8] & (1 << (bit % 8))) != 0;
}

void SetBit(unsigned char* mask, int bit) { mask[bit / 8] = static_cast<unsigned char>(mask[bit / 8] | (1 << (bit % 8))); }

/// libinput marks touchpads with tapping, the older synaptics driver with its name.
bool IsTouchpad(int device) {
    int  count = 0;
    bool yes = false;
    Atom* props = XIListProperties(gX.display, device, &count);
    for (int i = 0; i < count && !yes; ++i)
        if (char* name = XGetAtomName(gX.display, props[i])) {
            yes = std::strcmp(name, "libinput Tapping Enabled") == 0 || std::strncmp(name, "Synaptics", 9) == 0;
            XFree(name);
        }
    if (props) XFree(props);
    return yes;
}

void FindDevices() {
    gX.axes.clear();
    gX.touchpad.clear();
    gX.last.clear();
    int count = 0;
    XIDeviceInfo* info = XIQueryDevice(gX.display, XIAllDevices, &count);
    for (int i = 0; i < count; ++i) {
        const XIDeviceInfo& d = info[i];
        if (d.use != XISlavePointer) continue;
        std::vector<Axis> found;
        for (int c = 0; c < d.num_classes; ++c)
            if (d.classes[c]->type == XIScrollClass) {
                const auto* s = reinterpret_cast<const XIScrollClassInfo*>(d.classes[c]);
                found.push_back({s->number, s->scroll_type == XIScrollTypeVertical, s->increment != 0.0 ? std::abs(s->increment) : 1.0});
            }
        if (found.empty()) continue;
        gX.axes[d.deviceid] = found;
        gX.touchpad[d.deviceid] = IsTouchpad(d.deviceid);
    }
    if (info) XIFreeDeviceInfo(info);
}

void OnMotion(const XIDeviceEvent& e) {
    const auto axes = gX.axes.find(e.sourceid);
    if (axes == gX.axes.end()) return;
    float dx = 0.0f, dy = 0.0f;
    bool  smooth = false;
    int   value = 0;   // values[] holds only the valuators set in the mask
    for (int bit = 0; bit < e.valuators.mask_len * 8; ++bit) {
        if (!MaskBit(e.valuators.mask, e.valuators.mask_len, bit)) continue;
        const double v = e.valuators.values[value++];
        for (const Axis& a : axes->second) {
            if (a.number != bit) continue;
            const long long key = (static_cast<long long>(e.sourceid) << 16) | bit;
            if (const auto it = gX.last.find(key); it != gX.last.end()) {
                const double d = v - it->second;
                const double steps = d / a.increment;
                if (std::abs(steps - std::round(steps)) > 1e-3) smooth = true;
                // The valuator grows scrolling down or right: the content goes up or left.
                (a.vertical ? dy : dx) -= static_cast<float>(d);
            }
            gX.last[key] = v;
        }
    }
    if (dx == 0.0f && dy == 0.0f) return;
    const bool fingers = FingersNow(smooth);   // updates the burst either way
    if (!gX.touchpad[e.sourceid] && !fingers) return;
    TouchEvent t;
    t.dx = dx;
    t.dy = dy;
    detail::PushTouch(t);
}

#if defined(BZ_XI_GESTURES)
void OnPinch(const XIGesturePinchEvent& e) {
    if (e.scale > 0.0 && gX.pinchScale > 0.0 && e.scale != gX.pinchScale) {
        TouchEvent t;
        t.gesture = TouchGesture::Pinch;
        t.amount = static_cast<float>(e.scale / gX.pinchScale - 1.0);
        detail::PushTouch(t);
    }
    gX.pinchScale = e.scale;
    if (e.delta_angle != 0.0) {
        TouchEvent t;
        t.gesture = TouchGesture::Rotate;
        t.amount = static_cast<float>(-e.delta_angle);   // X: clockwise positive
        detail::PushTouch(t);
    }
}
#endif

void Pump() {
    if (!gX.display) return;
    gX.pending = false;
    while (XPending(gX.display) > 0) {
        XEvent event;
        XNextEvent(gX.display, &event);
        XGenericEventCookie* cookie = &event.xcookie;
        if (cookie->type != GenericEvent || cookie->extension != gX.opcode || !XGetEventData(gX.display, cookie)) continue;
        switch (cookie->evtype) {
            case XI_HierarchyChanged: FindDevices(); break;
            case XI_Enter: gX.last.clear(); break;   // valuators may jump while the pointer is elsewhere
            case XI_Motion: OnMotion(*static_cast<const XIDeviceEvent*>(cookie->data)); break;
#if defined(BZ_XI_GESTURES)
            case XI_GesturePinchBegin: gX.pinchScale = 1.0; break;
            case XI_GesturePinchUpdate: OnPinch(*static_cast<const XIGesturePinchEvent*>(cookie->data)); break;
#endif
            default: break;
        }
        XFreeEventData(gX.display, cookie);
    }
}

void CloseX11() {
    if (gX.waker.joinable()) {
        gX.stopping = true;
        (void)!::write(gX.stop[1], "x", 1);
        gX.waker.join();
    }
    for (int& fd : gX.stop)
        if (fd >= 0) ::close(std::exchange(fd, -1));
    if (gX.display) XCloseDisplay(std::exchange(gX.display, nullptr));
    gX.axes.clear();
    gX.touchpad.clear();
    gX.last.clear();
    gX.gestures = false;
    gX.stopping = false;
}

#else

void Pump() {}

#endif

} // namespace

void detail::PushTouch(const TouchEvent& event) {
    if (gEvents.size() < kMaxQueued) gEvents.push_back(event);
}

std::vector<TouchEvent> TakeTouchEvents() {
    Pump();
    return std::exchange(gEvents, {});
}

#if !defined(__APPLE__)

void SetSmoothScrollIsTouchpad(bool on) { gSmoothIsTouchpad = on; }

#if defined(_WIN32)

void StartTouchpad(GLFWwindow* window) {
    if (gWindow || !window) return;
    gWindow = glfwGetWin32Window(window);
    if (!gWindow) return;
    gPreviousProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gWindow, GWLP_WNDPROC));
    SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&OnMessage));
    log::Info("Touchpad: Windows Precision Touchpad messages (scrolling, pinch)");
}

void StopTouchpad() {
    if (!gWindow) return;
    SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gPreviousProc));
    gWindow = nullptr;
    gPreviousProc = nullptr;
}

TouchSupport TouchpadSupport() { return {true, true, false, false, false}; }

#elif defined(__linux__)

void StartTouchpad(GLFWwindow* window) {
    if (gX.display || !window) return;
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) return;
#endif
    gX.window = glfwGetX11Window(window);
    if (!gX.window || !(gX.display = XOpenDisplay(nullptr))) return;
    int event = 0, error = 0, major = 2, minor = 4;
    if (!XQueryExtension(gX.display, "XInputExtension", &gX.opcode, &event, &error) ||
        XIQueryVersion(gX.display, &major, &minor) != Success || major < 2 || (major == 2 && minor < 1)) {
        log::Info("Touchpad: no XInput 2.1 on this X server; touchpad scrolling works as a wheel");
        CloseX11();
        return;
    }
#if defined(BZ_XI_GESTURES)
    gX.gestures = major > 2 || minor >= 4;
#endif
    FindDevices();

    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {};
    SetBit(bits, XI_Motion);
    SetBit(bits, XI_Enter);
#if defined(BZ_XI_GESTURES)
    if (gX.gestures) {
        SetBit(bits, XI_GesturePinchBegin);
        SetBit(bits, XI_GesturePinchUpdate);
        SetBit(bits, XI_GesturePinchEnd);
    }
#endif
    XIEventMask mask{XIAllMasterDevices, static_cast<int>(sizeof bits), bits};
    unsigned char rootBits[XIMaskLen(XI_LASTEVENT)] = {};
    SetBit(rootBits, XI_HierarchyChanged);
    XIEventMask rootMask{XIAllDevices, static_cast<int>(sizeof rootBits), rootBits};
    // An X error here would end the program through Xlib's default handler.
    gXFailed = false;
    auto* const previous = XSetErrorHandler(OnXError);
    XISelectEvents(gX.display, gX.window, &mask, 1);
    XISelectEvents(gX.display, DefaultRootWindow(gX.display), &rootMask, 1);
    XSync(gX.display, False);
    XSetErrorHandler(previous);
    if (gXFailed || ::pipe(gX.stop) != 0) {
        log::Warn("Touchpad: the X server refused the touchpad events");
        CloseX11();
        return;
    }
    gX.fd = ConnectionNumber(gX.display);
    gX.waker = std::thread([] {
        pollfd fds[2] = {{gX.fd, POLLIN, 0}, {gX.stop[0], POLLIN, 0}};
        while (!gX.stopping) {
            fds[0].revents = fds[1].revents = 0;
            if (::poll(fds, 2, -1) < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (fds[1].revents != 0 || (fds[0].revents & (POLLERR | POLLHUP)) != 0) break;
            if (!gX.pending.exchange(true)) glfwPostEmptyEvent();
            // Until the main thread has read it, the socket stays readable.
            while (gX.pending && !gX.stopping) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::size_t touchpads = 0;
    for (const auto& [id, yes] : gX.touchpad) touchpads += yes ? 1 : 0;
    log::Info("Touchpad: XInput 2.{}, {} scrolling devices ({} touchpads), pinch and rotation {}", minor, gX.axes.size(), touchpads,
              gX.gestures ? "on" : "not offered by the X server");
}

void StopTouchpad() { CloseX11(); }

TouchSupport TouchpadSupport() { return {gX.display != nullptr, gX.gestures, gX.gestures, false, false}; }

#else

void StartTouchpad(GLFWwindow*) {}
void StopTouchpad() {}
TouchSupport TouchpadSupport() { return {}; }

#endif
#endif   // !__APPLE__

} // namespace bz::platform
