#include "app/android/AndroidInput.h"

#include "platform/Android.h"
#include "platform/Window.h"

#include <chrono>
#include <android/input.h>
#include <android/keycodes.h>

namespace bz::app::android {
namespace {

ImGuiKey KeyOf(int32_t code) {
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z) return static_cast<ImGuiKey>(ImGuiKey_A + (code - AKEYCODE_A));
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9) return static_cast<ImGuiKey>(ImGuiKey_0 + (code - AKEYCODE_0));
    if (code >= AKEYCODE_F1 && code <= AKEYCODE_F12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (code - AKEYCODE_F1));
    if (code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (code - AKEYCODE_NUMPAD_0));
    switch (code) {
        case AKEYCODE_TAB:             return ImGuiKey_Tab;
        case AKEYCODE_DPAD_LEFT:       return ImGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT:      return ImGuiKey_RightArrow;
        case AKEYCODE_DPAD_UP:         return ImGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN:       return ImGuiKey_DownArrow;
        case AKEYCODE_PAGE_UP:         return ImGuiKey_PageUp;
        case AKEYCODE_PAGE_DOWN:       return ImGuiKey_PageDown;
        case AKEYCODE_MOVE_HOME:       return ImGuiKey_Home;
        case AKEYCODE_MOVE_END:        return ImGuiKey_End;
        case AKEYCODE_INSERT:          return ImGuiKey_Insert;
        case AKEYCODE_FORWARD_DEL:     return ImGuiKey_Delete;
        case AKEYCODE_DEL:             return ImGuiKey_Backspace;
        case AKEYCODE_SPACE:           return ImGuiKey_Space;
        case AKEYCODE_ENTER:           return ImGuiKey_Enter;
        case AKEYCODE_NUMPAD_ENTER:    return ImGuiKey_Enter;   // one Enter for shortcuts, as on the desktop
        case AKEYCODE_ESCAPE:          return ImGuiKey_Escape;
        case AKEYCODE_APOSTROPHE:      return ImGuiKey_Apostrophe;
        case AKEYCODE_COMMA:           return ImGuiKey_Comma;
        case AKEYCODE_MINUS:           return ImGuiKey_Minus;
        case AKEYCODE_PERIOD:          return ImGuiKey_Period;
        case AKEYCODE_SLASH:           return ImGuiKey_Slash;
        case AKEYCODE_SEMICOLON:       return ImGuiKey_Semicolon;
        case AKEYCODE_EQUALS:          return ImGuiKey_Equal;
        case AKEYCODE_LEFT_BRACKET:    return ImGuiKey_LeftBracket;
        case AKEYCODE_BACKSLASH:       return ImGuiKey_Backslash;
        case AKEYCODE_RIGHT_BRACKET:   return ImGuiKey_RightBracket;
        case AKEYCODE_GRAVE:           return ImGuiKey_GraveAccent;
        case AKEYCODE_NUMPAD_DOT:      return ImGuiKey_KeypadDecimal;
        case AKEYCODE_NUMPAD_DIVIDE:   return ImGuiKey_KeypadDivide;
        case AKEYCODE_NUMPAD_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case AKEYCODE_NUMPAD_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case AKEYCODE_NUMPAD_ADD:      return ImGuiKey_KeypadAdd;
        case AKEYCODE_CTRL_LEFT:       return ImGuiKey_LeftCtrl;
        case AKEYCODE_CTRL_RIGHT:      return ImGuiKey_RightCtrl;
        case AKEYCODE_SHIFT_LEFT:      return ImGuiKey_LeftShift;
        case AKEYCODE_SHIFT_RIGHT:     return ImGuiKey_RightShift;
        case AKEYCODE_ALT_LEFT:        return ImGuiKey_LeftAlt;
        case AKEYCODE_ALT_RIGHT:       return ImGuiKey_RightAlt;
        case AKEYCODE_MENU:            return ImGuiKey_Menu;
        default:                       return ImGuiKey_None;
    }
}

void PressKey(ImGuiKey key) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
}

mobile::PointerTool ToolOf(int32_t type) {
    switch (type) {
        case AMOTION_EVENT_TOOL_TYPE_STYLUS: return mobile::PointerTool::Stylus;
        case AMOTION_EVENT_TOOL_TYPE_ERASER: return mobile::PointerTool::Eraser;
        case AMOTION_EVENT_TOOL_TYPE_MOUSE:  return mobile::PointerTool::Mouse;
        default:                             return mobile::PointerTool::Finger;
    }
}

int Motion(const AInputEvent* e, const InputSink& sink) {
    const int32_t raw = AMotionEvent_getAction(e);
    const int32_t action = raw & AMOTION_EVENT_ACTION_MASK;
    const auto index = static_cast<std::size_t>((raw & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const std::size_t count = AMotionEvent_getPointerCount(e);
    const int32_t state = AMotionEvent_getButtonState(e);
    const double now = platform::Window::Time();

    const auto make = [&](std::size_t i, mobile::PointerAction a) {
        mobile::PointerEvent p;
        p.id = AMotionEvent_getPointerId(e, i);
        p.tool = ToolOf(AMotionEvent_getToolType(e, i));
        p.action = a;
        p.x = AMotionEvent_getX(e, i) / sink.density;
        p.y = AMotionEvent_getY(e, i) / sink.density;
        p.time = now;
        if (p.tool == mobile::PointerTool::Mouse) {
            p.buttons = (state & AMOTION_EVENT_BUTTON_PRIMARY ? 1 : 0) | (state & AMOTION_EVENT_BUTTON_SECONDARY ? 2 : 0) |
                        (state & AMOTION_EVENT_BUTTON_TERTIARY ? 4 : 0);
            // A touchpad's or an old mouse's tap comes without a button state: the primary one.
            if (p.buttons == 0 && (a == mobile::PointerAction::Down || a == mobile::PointerAction::Move)) p.buttons = 1;
        } else {
            // A pen's barrel button: its own code, or the secondary button on older pens.
            p.buttons = (state & (AMOTION_EVENT_BUTTON_STYLUS_PRIMARY | AMOTION_EVENT_BUTTON_SECONDARY)) ? 2 : 0;
        }
        return p;
    };

    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            sink.pointer(make(index, mobile::PointerAction::Down));
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            sink.pointer(make(index, mobile::PointerAction::Up));
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            for (std::size_t i = 0; i < count; ++i) sink.pointer(make(i, mobile::PointerAction::Move));
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            for (std::size_t i = 0; i < count; ++i) sink.pointer(make(i, mobile::PointerAction::Cancel));
            break;
        case AMOTION_EVENT_ACTION_HOVER_ENTER:
        case AMOTION_EVENT_ACTION_HOVER_MOVE:
            sink.pointer(make(0, mobile::PointerAction::HoverMove));
            break;
        case AMOTION_EVENT_ACTION_HOVER_EXIT:
            sink.pointer(make(0, mobile::PointerAction::HoverExit));
            break;
        case AMOTION_EVENT_ACTION_SCROLL: {
            mobile::PointerEvent p = make(0, mobile::PointerAction::Scroll);
            p.tool = mobile::PointerTool::Mouse;
            p.scrollX = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HSCROLL, 0);
            p.scrollY = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_VSCROLL, 0);
            sink.pointer(p);
            break;
        }
        case AMOTION_EVENT_ACTION_BUTTON_PRESS:
        case AMOTION_EVENT_ACTION_BUTTON_RELEASE:
            if (ToolOf(AMotionEvent_getToolType(e, 0)) == mobile::PointerTool::Mouse) sink.pointer(make(0, mobile::PointerAction::Move));
            break;
        default: return 0;
    }
    sink.woke();
    return 1;
}

int Key(const AInputEvent* e, const InputSink& sink) {
    const int32_t code = AKeyEvent_getKeyCode(e);
    const int32_t action = AKeyEvent_getAction(e);
    const int32_t meta = AKeyEvent_getMetaState(e);
    const bool down = action == AKEY_EVENT_ACTION_DOWN;
    sink.woke();

    if (code == AKEYCODE_BACK) {
        // "Back" is Esc inside the program; with nothing left to close, the system's.
        if (sink.backLeaves && sink.backLeaves()) {
            // Leaving takes two: the first "back" (often a stray edge swipe) only says so,
            // a second within 3 s goes to the system (the author, 18.10.2026).
            static std::chrono::steady_clock::time_point armedUntil{};
            static bool passing = false;
            if (down) {
                const auto now = std::chrono::steady_clock::now();
                passing = now < armedUntil;
                if (!passing) {
                    armedUntil = now + std::chrono::seconds(3);
                    if (sink.backHint) sink.backHint();
                }
            }
            return passing ? 0 : 1;
        }
        if (action == AKEY_EVENT_ACTION_UP) {
            PressKey(ImGuiKey_Escape);
            sink.chord(ImGuiKey_Escape);
        }
        return 1;
    }
    if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN) {
        if (!sink.volume) return 0;
        if (!down) return platform::android::VolumeKeysTaken() ? 1 : 0;
        return sink.volume(code == AKEYCODE_VOLUME_UP ? 1 : -1) ? 1 : 0;
    }
    if (action == AKEY_EVENT_ACTION_MULTIPLE) return 0;

    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (meta & AMETA_CTRL_ON) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (meta & AMETA_SHIFT_ON) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (meta & AMETA_ALT_ON) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (meta & AMETA_META_ON) != 0);
    const ImGuiKey key = KeyOf(code);
    if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
    if (down) {
        if (key != ImGuiKey_None) {
            ImGuiKeyChord chord = key == ImGuiKey_KeypadEnter ? ImGuiKey_Enter : key;
            if (meta & AMETA_CTRL_ON) chord |= ImGuiMod_Ctrl;
            if (meta & AMETA_SHIFT_ON) chord |= ImGuiMod_Shift;
            if (meta & AMETA_ALT_ON) chord |= ImGuiMod_Alt;
            sink.chord(chord);
        }
        // The character the key makes (a hardware keyboard).
        if (!(meta & (AMETA_CTRL_ON | AMETA_ALT_ON | AMETA_META_ON)))
            if (const int ch = platform::android::UnicodeChar(AInputEvent_getDeviceId(e), code, meta); ch >= 32 && ch != 127)
                io.AddInputCharacter(static_cast<unsigned>(ch));
    }
    return key != ImGuiKey_None ? 1 : 0;
}

} // namespace

int HandleInput(const void* event, const InputSink& sink) {
    const auto* e = static_cast<const AInputEvent*>(event);
    switch (AInputEvent_getType(e)) {
        case AINPUT_EVENT_TYPE_MOTION: return Motion(e, sink);
        case AINPUT_EVENT_TYPE_KEY:    return Key(e, sink);
        default:                       return 0;
    }
}

void FeedTypedText() {
    platform::android::TypedText typed = platform::android::TakeTypedText();
    ImGuiIO& io = ImGui::GetIO();
    for (const platform::android::TypedItem& item : typed) {
        if (item.key == 0) {
            io.AddInputCharactersUTF8(item.text.c_str());
            continue;
        }
        switch (item.key) {
            case AKEYCODE_DEL:         PressKey(ImGuiKey_Backspace); break;
            case AKEYCODE_FORWARD_DEL: PressKey(ImGuiKey_Delete); break;
            case AKEYCODE_ENTER:       PressKey(ImGuiKey_Enter); break;
            case AKEYCODE_TAB:         PressKey(ImGuiKey_Tab); break;
            default: break;
        }
    }
}

} // namespace bz::app::android
