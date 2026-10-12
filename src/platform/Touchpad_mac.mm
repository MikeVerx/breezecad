// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 MikeVerx
// Touchpad gestures on macOS: Cocoa reports them all (scrolling with phases,
// magnify, rotate, smart magnify, pressure). A local event monitor sees each
// event of the window before GLFW does and lets it through unchanged.
#include "platform/Touchpad.h"

#include "core/Log.h"

#import <Cocoa/Cocoa.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

namespace bz::platform {
namespace {

id        gMonitor = nil;
NSWindow* gWindow = nil;
NSInteger gStage = 0;   ///< pressure stage of the click in progress (2 = force click)

/// Returns true when the event became a gesture.
bool Translate(NSEvent* e) {
    TouchEvent t;
    switch (e.type) {
        case NSEventTypeScrollWheel:
            // Fingers (trackpad, Magic Mouse) scroll precisely and in phases;
            // a wheel does neither.
            if (!e.hasPreciseScrollingDeltas || (e.phase == NSEventPhaseNone && e.momentumPhase == NSEventPhaseNone)) return false;
            t.dx = static_cast<float>(e.scrollingDeltaX);
            t.dy = static_cast<float>(e.scrollingDeltaY);
            t.momentum = e.momentumPhase != NSEventPhaseNone;
            break;
        case NSEventTypeMagnify:
            t.gesture = TouchGesture::Pinch;
            t.amount = static_cast<float>(e.magnification);
            break;
        case NSEventTypeRotate:
            t.gesture = TouchGesture::Rotate;
            t.amount = e.rotation;   // degrees, counter-clockwise positive
            break;
        case NSEventTypeSmartMagnify: t.gesture = TouchGesture::SmartZoom; break;
        case NSEventTypePressure: {
            const NSInteger stage = e.stage;
            const bool deeper = stage >= 2 && gStage < 2;
            gStage = stage;
            if (!deeper) return false;
            t.gesture = TouchGesture::ForceClick;
            break;
        }
        default: return false;
    }
    detail::PushTouch(t);
    return true;
}

} // namespace

void StartTouchpad(GLFWwindow* window) {
    if (gMonitor || !window) return;
    gWindow = glfwGetCocoaWindow(window);
    if (!gWindow) return;
    const NSEventMask mask = NSEventMaskScrollWheel | NSEventMaskMagnify | NSEventMaskRotate | NSEventMaskSmartMagnify |
                             NSEventMaskPressure;
    gMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                     handler:^NSEvent*(NSEvent* event) {
                                                         if (event.window == gWindow) Translate(event);
                                                         return event;   // GLFW still gets it (lists keep scrolling)
                                                     }];
    log::Info("Touchpad: Cocoa gestures (scrolling, pinch, rotation, smart zoom, force click)");
}

void StopTouchpad() {
    if (gMonitor) [NSEvent removeMonitor:gMonitor];
    gMonitor = nil;
    gWindow = nil;
}

TouchSupport TouchpadSupport() { return {true, true, true, true, true}; }

// Cocoa tells fingers from a wheel itself: nothing to guess.
void SetSmoothScrollIsTouchpad(bool) {}

} // namespace bz::platform
