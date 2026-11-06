// The iOS part of Application (docs/dev/IOS-PORT.md): input from the view —
// fingers and the Apple Pencil as pointers to the touch input (app/mobile),
// the system keyboard's text into ImGui. The rest of the mobile app is
// Application_mobile.cpp, shared with Android.
#if defined(BZ_IOS)

#include "app/Application.h"
#include "app/mobile/MobileLayout.h"
#include "app/mobile/TouchInput.h"
#include "platform/Mobile.h"
#include "platform/Window.h"

#include <imgui.h>

namespace bz::app {

namespace {

mobile::PointerAction ActionOf(int phase) {
    switch (phase) {
        case 0:  return mobile::PointerAction::Down;
        case 2:  return mobile::PointerAction::Up;
        case 3:  return mobile::PointerAction::Cancel;
        default: return mobile::PointerAction::Move;
    }
}

mobile::PointerTool ToolOf(int tool) {
    switch (tool) {
        case 1:  return mobile::PointerTool::Stylus;
        case 2:  return mobile::PointerTool::Mouse;
        default: return mobile::PointerTool::Finger;
    }
}

void PressKey(ImGuiKey key) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
}

} // namespace

void Application::StartMobile() {
    myShell.mobile.touchScreen = true;
    myShell.mobile.phone = mobile::WantsPhoneLayout(mySettings, myWindow);
    myStartedAt = platform::Window::Time();

    myWindow.SetTouchHandler([this](const platform::Window::Touch& t) {
        if (!ImGui::GetCurrentContext()) return;
        mobile::PointerEvent e;
        e.id = t.id;
        e.tool = ToolOf(t.tool);
        e.action = ActionOf(t.phase);
        e.x = t.x;
        e.y = t.y;
        e.buttons = t.tool == 2 && t.phase != 2 ? 1 : 0;   // a trackpad or mouse pointer pressed: the left button
        e.time = t.time;
        myTouch.Push(e);
        myInput.seen = true;
        ++myInput.wakes[static_cast<std::size_t>(Wake::Button)];
    });
    myWindow.SetChangeHandler([this] {
        myInput.seen = true;
        ++myInput.wakes[static_cast<std::size_t>(Wake::Size)];
    });
}

void Application::FeedSystemInput() {
    // The keyboard's text; its special keys come as Android key codes (Mobile.h).
    constexpr int kEnter = 66, kBackspace = 67, kTab = 61;
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    for (const platform::mobile::TypedItem& item : platform::mobile::TakeTypedText()) {
        if (item.key == 0) {
            io.AddInputCharactersUTF8(item.text.c_str());
            myInput.seen = true;
            continue;
        }
        if (item.key == kBackspace) PressKey(ImGuiKey_Backspace);
        else if (item.key == kEnter) PressKey(ImGuiKey_Enter);
        else if (item.key == kTab) PressKey(ImGuiKey_Tab);
        myInput.seen = true;
    }
}

} // namespace bz::app

#endif
