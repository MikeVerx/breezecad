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

/// A hardware keyboard's key (USB HID usage, UIKeyboardHIDUsage) as ImGui's.
ImGuiKey KeyOf(int usage) {
    if (usage >= 0x04 && usage <= 0x1D) return static_cast<ImGuiKey>(ImGuiKey_A + (usage - 0x04));
    if (usage >= 0x1E && usage <= 0x26) return static_cast<ImGuiKey>(ImGuiKey_1 + (usage - 0x1E));
    if (usage == 0x27) return ImGuiKey_0;
    if (usage >= 0x3A && usage <= 0x45) return static_cast<ImGuiKey>(ImGuiKey_F1 + (usage - 0x3A));
    if (usage >= 0x59 && usage <= 0x61) return static_cast<ImGuiKey>(ImGuiKey_Keypad1 + (usage - 0x59));
    switch (usage) {
        case 0x28: return ImGuiKey_Enter;
        case 0x29: return ImGuiKey_Escape;
        case 0x2A: return ImGuiKey_Backspace;
        case 0x2B: return ImGuiKey_Tab;
        case 0x2C: return ImGuiKey_Space;
        case 0x2D: return ImGuiKey_Minus;
        case 0x2E: return ImGuiKey_Equal;
        case 0x2F: return ImGuiKey_LeftBracket;
        case 0x30: return ImGuiKey_RightBracket;
        case 0x31: return ImGuiKey_Backslash;
        case 0x33: return ImGuiKey_Semicolon;
        case 0x34: return ImGuiKey_Apostrophe;
        case 0x35: return ImGuiKey_GraveAccent;
        case 0x36: return ImGuiKey_Comma;
        case 0x37: return ImGuiKey_Period;
        case 0x38: return ImGuiKey_Slash;
        case 0x49: return ImGuiKey_Insert;
        case 0x4A: return ImGuiKey_Home;
        case 0x4B: return ImGuiKey_PageUp;
        case 0x4C: return ImGuiKey_Delete;
        case 0x4D: return ImGuiKey_End;
        case 0x4E: return ImGuiKey_PageDown;
        case 0x4F: return ImGuiKey_RightArrow;
        case 0x50: return ImGuiKey_LeftArrow;
        case 0x51: return ImGuiKey_DownArrow;
        case 0x52: return ImGuiKey_UpArrow;
        case 0x54: return ImGuiKey_KeypadDivide;
        case 0x55: return ImGuiKey_KeypadMultiply;
        case 0x56: return ImGuiKey_KeypadSubtract;
        case 0x57: return ImGuiKey_KeypadAdd;
        case 0x58: return ImGuiKey_KeypadEnter;
        case 0x62: return ImGuiKey_Keypad0;
        case 0x63: return ImGuiKey_KeypadDecimal;
        case 0xE0: return ImGuiKey_LeftCtrl;
        case 0xE1: return ImGuiKey_LeftShift;
        case 0xE2: return ImGuiKey_LeftAlt;
        case 0xE3: return ImGuiKey_LeftSuper;
        case 0xE4: return ImGuiKey_RightCtrl;
        case 0xE5: return ImGuiKey_RightShift;
        case 0xE6: return ImGuiKey_RightAlt;
        case 0xE7: return ImGuiKey_RightSuper;
        default:   return ImGuiKey_None;
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
    // A hardware keyboard: keys for ImGui and the shortcuts (Cmd is Ctrl here,
    // as iPad users press Cmd+S, Cmd+Z); its characters come as typed text.
    myWindow.SetKeyHandler([this](const platform::Window::Key& k) {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        const bool ctrl = k.ctrl || k.command;
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, k.shift);
        io.AddKeyEvent(ImGuiMod_Alt, k.alt);
        const ImGuiKey key = KeyOf(k.usage);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, k.down);
        if (k.down && key != ImGuiKey_None) {
            ImGuiKeyChord chord = key == ImGuiKey_KeypadEnter ? ImGuiKey_Enter : key;
            if (ctrl) chord |= ImGuiMod_Ctrl;
            if (k.shift) chord |= ImGuiMod_Shift;
            if (k.alt) chord |= ImGuiMod_Alt;
            myInput.chords.push_back(chord);
        }
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
