#pragma once
// iOS and iPadOS: what only UIKit has (docs/dev/IOS-PORT.md). The calls every
// mobile system has are in platform/Mobile.h (Mobile_ios.mm here).
//
// Threads: UIKit owns the main thread, and the program runs on it too — its
// loop (app/Application) pumps the main run loop in Window::WaitEvents, as
// SDL does on iOS. So the view's touches, the keyboard and the system's
// answers arrive between frames on the program's own thread, and every call
// here is made from it.

#if defined(BZ_IOS)

#include "platform/Window.h"

#include <filesystem>
#include <string>

namespace bz::platform::ios {

/// The app's view (a UIView with a CAEAGLLayer, app/ios/IosMain.mm), set
/// once before the program starts; the window draws into it.
void  SetView(void* view);
void* View();

/// The app's bundle (BreezeCAD.app): the assets lie in it, read-only.
std::filesystem::path BundleDir();

// --- From the view (IosMain.mm) to the window and the program ---------------

/// A finger, pencil or pointer of the view.
void DeliverTouch(const Window::Touch& touch);
/// A key of a hardware keyboard.
void DeliverKeyPress(const Window::Key& key);
/// The view's size, safe area or the keyboard changed.
void ViewChanged();
/// Text from the system keyboard (UIKeyInput): characters, or a key (see Mobile.h, TypedItem).
void DeliverText(const std::string& text);
void DeliverKey(int key);
/// The keyboard's height over the view's bottom (points), from its notices.
void KeyboardMoved(float height);
/// The safe area changed: the layout reads the screen again (Mobile.h, CurrentScreen).
void ScreenMoved();
/// The app went to the background (no OpenGL there) or came back.
void SetInBackground(bool background);
bool InBackground();
/// The system is ending the app.
void SetClosing();
bool Closing();

/// The view asks whether the system keyboard should show (a text field has focus).
bool WantsKeyboard();

/// The system's dark appearance (UITraitCollection).
bool PrefersDark();

} // namespace bz::platform::ios

#endif
