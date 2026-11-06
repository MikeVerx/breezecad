// The Android part of Application (docs/ADR/0004-mobile-touch-ui.md): input
// from the window (AInputEvent, app/android/AndroidInput); the rest of the
// mobile app is Application_mobile.cpp, shared with iOS.
#if defined(__ANDROID__)

#include "app/Application.h"
#include "app/android/AndroidInput.h"
#include "app/mobile/MobileLayout.h"
#include "app/mobile/TouchInput.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "platform/Android.h"

#include <imgui.h>

#include <algorithm>

namespace bz::app {

namespace droid = platform::android;   // app::android is the input (android/AndroidInput.h)

void Application::StartMobile() {
    myShell.mobile.touchScreen = true;
    myShell.mobile.phone = mobile::WantsPhoneLayout(mySettings, myWindow);
    myStartedAt = platform::Window::Time();

    android::InputSink sink;
    sink.density = myWindow.Density();
    sink.pointer = [this](const mobile::PointerEvent& e) { myTouch.Push(e); };
    sink.chord = [this](ImGuiKeyChord chord) { myInput.chords.push_back(chord); };
    sink.woke = [this] {
        myInput.seen = true;
        ++myInput.wakes[static_cast<std::size_t>(Wake::Button)];
    };
    sink.backLeaves = [this] {
        // "Back" on the home page with nothing open: the system's (leave the app).
        return myWorkspace.ShowsHome() && !myShell.showSettings && !myShell.newDocument.open && !myShell.showAbout &&
               !myShell.mobile.lastCrash.open && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    };
    sink.backHint = [this] {
        log::Info("Back: once more to leave");
        myNotes.Info(Tr("Проведите «Назад» ещё раз, чтобы выйти"));
    };
    sink.volume = [this](int direction) {
        if (!mySettings.mobile.volumeZoom || myWorkspace.ShowsHome() || !myWorkspace.Active()) return false;
        const ImVec2 a = myShell.mobile.viewMin, b = myShell.mobile.viewMax;
        myViewport.ZoomAt(direction > 0 ? 1.2 : 1.0 / 1.2, {(b.x - a.x) / 2.0, (b.y - a.y) / 2.0});
        myFramesToDraw = std::max(myFramesToDraw, 3);
        return true;
    };
    myWindow.SetInputHandler([this, sink](const void* event) {
        if (!ImGui::GetCurrentContext()) return 0;
        android::InputSink s = sink;
        s.density = myWindow.Density();
        return android::HandleInput(event, s);
    });
    myWindow.SetChangeHandler([this] {
        myInput.seen = true;
        ++myInput.wakes[static_cast<std::size_t>(Wake::Size)];
    });
}

void Application::FeedSystemInput() {
    droid::SetVolumeKeysTaken(mySettings.mobile.volumeZoom);
    android::FeedTypedText();
}

} // namespace bz::app

#endif
