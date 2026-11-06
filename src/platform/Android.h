#pragma once
// Android: the app's activity and what only Java can do (docs/ADR/0004-mobile-touch-ui.md,
// "Что менять в коде"). The activity is io.github.mikeverx.breezecad.BreezeActivity
// (resources/android/java), a NativeActivity with a few helpers: the system's
// file pickers (SAF), "share", the keyboard and its height, the screen's
// cut-out, corners and gesture areas, immersive mode, the crash watchdog
// and why the last run ended.
//
// Calls into Java are made from the app's thread (android_main) and return
// at once; what Java answers later (a picked file, new insets) is kept here
// and taken by the app between frames. Java's callbacks wake the loop up.

#if defined(__ANDROID__)

#include "platform/Mobile.h"

struct android_app;

namespace bz::platform::android {

/// The glue's state, set once at the start of android_main.
void          SetApp(android_app* app);
android_app*  App();

// The calls every mobile system has (folders, screen, keyboard, files,
// "share", crashes, clipboard) are in platform/Mobile.h; android:: names them
// too, so the Android code says android::DataDir as before.
using namespace bz::platform::mobile;

/// The volume keys zoom the view (a setting): the app takes them instead of
/// the system's volume.
void SetVolumeKeysTaken(bool taken);
bool VolumeKeysTaken();

/// The character a hardware key makes (KeyCharacterMap), 0 if none.
int UnicodeChar(int deviceId, int keyCode, int metaState);

} // namespace bz::platform::android

#endif
