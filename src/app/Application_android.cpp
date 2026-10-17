// The Android part of Application (docs/ADR/0004-mobile-touch-ui.md): input
// from the window, the system's answers between frames, the keyboard, the
// background, and the check of how the last run ended.
#if defined(__ANDROID__)

#include "app/Application.h"
#include "app/Operations.h"
#include "app/android/AndroidInput.h"
#include "app/mobile/MobileLayout.h"
#include "app/mobile/TouchInput.h"
#include "app/sketch/SketchPick.h"
#include "core/CrashCode.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "document/Origin.h"
#include "platform/Android.h"
#include "platform/Paths.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace bz::app {

namespace droid = platform::android;   // app::android is the input (android/AndroidInput.h)

void Application::StartAndroid() {
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

void Application::InstallAndroidClipboard() {
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { droid::SetClipboard(text ? text : ""); };
    pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
        static std::string text;
        text = droid::Clipboard();
        return text.c_str();
    };
}

bool Application::AndroidBetweenFrames(double now) {
    droid::Heartbeat();
    droid::SetVolumeKeysTaken(mySettings.mobile.volumeZoom);

    // In the background the system may end the app at any moment without a
    // word: everything unsaved goes to the autosave at once.
    const bool background = !myWindow.CanDraw();
    if ((background && !myInBackground) || myWindow.ShouldClose()) {
        for (auto& s : myWorkspace.sessions) myAutosave->WriteNow(*s, now);
        mySettings.Save(platform::ConfigDir() / "settings.json");
        log::Info("Android: {} — work kept in the autosave", myWindow.ShouldClose() ? "the system closes the app" : "in the background");
    }
    if (myWindow.ShouldClose()) return false;
    if (!background && myInBackground) myFramesToDraw = std::max(myFramesToDraw, 3);
    myInBackground = background;

    // The screen around the window: cut-out, bars, corners, keyboard.
    const droid::Screen screen = droid::CurrentScreen();
    if (screen.revision != myScreenRevision) {
        myScreenRevision = screen.revision;
        mobile::MobileState& m = myShell.mobile;
        // In landscape the camera is on the left or the right (it moves when the phone turns over).
        m.cutoutSide = screen.cutout.left > 0.5f ? -1 : screen.cutout.right > 0.5f ? 1 : 0;
        m.cutoutFrom = screen.cutoutFrom;
        m.cutoutTo = screen.cutoutTo;
        m.cornerRadius = screen.cornerRadius;
        m.insets = {std::max(screen.cutout.left, screen.bars.left), std::max(screen.cutout.top, screen.bars.top),
                    std::max(screen.cutout.right, screen.bars.right),
                    std::max({screen.cutout.bottom, screen.bars.bottom, screen.gestures.bottom * 0.5f})};
        m.keyboardHeight = screen.ime.bottom;
        m.hardKeyboard = screen.hardKeyboard;
        myFramesToDraw = std::max(myFramesToDraw, 3);
    }
    android::FeedTypedText();

    // An answer of the system's picker that no dialog waited for: the projects folder.
    if (auto picked = droid::TakePickResult()) {
        if (picked->kind == droid::PickResult::Kind::Folder && !picked->uri.empty()) {
            mySettings.mobile.projectsFolderName = picked->name;
            myNotes.Success(Trf("Папка проектов: {}", picked->name));
        } else if (picked->kind == droid::PickResult::Kind::Open && !picked->uri.empty()) {
            if (const auto local = droid::Import(picked->uri, picked->name); !local.empty()) OpenDropped(*myContext, local);
        }
        myFramesToDraw = std::max(myFramesToDraw, 3);
    }

    // The keyboard: the numpad in block 1 for values on the phone, the
    // system's keyboard for names and search (or when asked with ⌨); none
    // when a hardware keyboard is there.
    mobile::MobileState& m = myShell.mobile;
    // A bare finger picks sketch lines from ~3 mm; the cursor's tip is precise.
    SetPickScale(m.phone && !mobile::CursorActive(*myContext) ? 3.0f : 1.0f);
    myContext->sketch.SetAutoArrange(m.phone);   // new dimensions placed clear of the others
    const bool typing = ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
    const bool words = myShell.paletteOpen || myShell.renaming || myShell.treeSearch || m.fullKeyboard || !m.phone;
    myUi->keyHints = m.hardKeyboard;   // Ctrl+N or Esc mean nothing on a touch screen without a keyboard
    const bool hadNumpad = m.numpad;
    m.numpad = m.phone && typing && !words && !m.hardKeyboard;
    // A value left any way (↵, the OK button, another field): it is "пред." for the next one.
    if (hadNumpad && !m.numpad && !m.editingText.empty()) m.previousValue = m.editingText;
    if (hadNumpad != m.numpad) log::Info("Keyboard: numpad {}", m.numpad ? "on" : "off");
    const bool keyboard = typing && words && !m.hardKeyboard;
    if (!typing) m.fullKeyboard = false;
    if (keyboard != myKeyboardUp) {
        log::Info("Keyboard: system {}", keyboard ? "on" : "off");
        droid::ShowKeyboard(keyboard);
        myKeyboardUp = keyboard;
    }

    // The watchdog's notice needs a permission (Android 13+): asked once, a
    // little after the start rather than over the splash.
    if (!mySettings.mobile.askedNotifications && now - myStartedAt > 3.0) {
        mySettings.mobile.askedNotifications = true;
        droid::AskNotifications();
    }
    return true;
}

void Application::CheckLastExit() {
    // Layer 3 (ADR-0004): how the last run ended, as the system says, and a
    // report our handler or the hang guard left. Either one: the error
    // screen, then the recovery on the home page as on the desktop.
    const std::string reason = droid::LastExitReason();
    const std::filesystem::path dir = platform::ConfigDir() / "reports";
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = ToUtf8(e.path().filename());
        if (!(name.starts_with("crash-") || name.starts_with("hang-")) || !name.ends_with(".txt")) continue;
        const auto t = e.last_write_time(ec);
        if (newest.empty() || t > newestTime) {
            newest = e.path();
            newestTime = t;
        }
    }
    // A report is news once: its name is noted when shown.
    const std::filesystem::path seenFile = dir / ".shown";
    std::string seen;
    std::getline(std::ifstream(seenFile), seen);
    const bool newReport = !newest.empty() && ToUtf8(newest.filename()) != seen;
    const bool systemSaysCrash = reason == "crash" || reason == "native crash" || reason == "anr";
    if (!newReport && !systemSaysCrash) return;

    mobile::MobileState::LastCrash& c = myShell.mobile.lastCrash;
    c.open = true;
    c.reason = reason;
    if (newReport) {
        std::ifstream in(newest, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        c.report = text.str();
        c.file = ToUtf8(newest);
        c.hang = ToUtf8(newest.filename()).starts_with("hang-");
        std::ofstream(seenFile, std::ios::trunc) << ToUtf8(newest.filename());
    } else {
        c.report = "BreezeCAD: the system reports the last run ended with: " + reason + "\n";
        c.hang = reason == "anr";
    }
    c.code = crash::ErrorCode(c.report);
    myShell.crashedLastTime = true;
    log::Warn("Android: the last run ended badly ({}), code {}", reason.empty() ? "a report was left" : reason, c.code);
}

void Application::RunTestCommands() {
    // A test run on the emulator (scripts/android/smoke-apk.sh): what to open
    // and do, so its screenshots show the program at work. Never set by a person.
    const std::string commands = droid::TestCommands();
    if (commands.empty()) return;
    log::Info("Android: test commands \"{}\"", commands);
    std::istringstream list(commands);
    for (std::string c; std::getline(list, c, ',');) {
        if (c == "example") {
            OpenDocument(*myContext, platform::AssetsDir() / "examples" / "bracket.bzc");
        } else if (c == "new") {
            NewDocument(*myContext);
        } else if (c == "sketch") {
            if (myWorkspace.Active() == nullptr) NewDocument(*myContext);
            if (Session* s = myWorkspace.Active()) {
                s->selectedFeature = doc::origin::kPlaneXY;   // "start a sketch" takes the chosen plane
                myActions.Run("sketch.start");
                myShell.ribbonTab = RibbonTab::Sketch;
            }
        } else if (c == "line") {
            myActions.Run("sketch.line");
        } else if (c == "cursor") {
            myShell.mobile.cursorIn3d = true;
        } else if (c == "lefty") {
            mySettings.mobile.leftHanded = true;
        } else if (c == "icons") {
            mySettings.mobile.iconsOnly = true;
        } else if (c == "desktop") {
            mySettings.mobile.layout = 1;
        } else if (c == "phone") {
            mySettings.mobile.layout = 2;
        } else if (c == "settings") {
            myShell.showSettings = true;
        } else if (c == "extrude") {
            myActions.Run("feature.extrude");   // its distance field: the emulator taps it (smoke-apk.sh)
        } else if (c == "numpad") {
            myShell.mobile.block1Tab = 1;
        } else if (c == "strip") {
            myShell.mobile.flyGroup = "*";   // compact block 2: the first group with more opens its strip
        } else if (c == "layout") {
            mobile::OpenLayoutEditor(*myContext);
        } else if (c == "setup") {
            mobile::StartSetup(*myContext);
        }
    }
    myFramesToDraw = std::max(myFramesToDraw, 5);
}

} // namespace bz::app

#endif
