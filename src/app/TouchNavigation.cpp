// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 MikeVerx
#include "app/TouchNavigation.h"

namespace bz::app {

TouchMove TouchNavigation::MoveWith(ModifierChoice modifier) const {
    switch (modifier) {
        case ModifierChoice::None:  return twoFingers;
        case ModifierChoice::Shift: return withShift;
        case ModifierChoice::Ctrl:  return withCtrl;
        case ModifierChoice::Alt:   return withAlt;
    }
    return TouchMove::None;
}

const std::vector<std::string_view>& TouchCommands() {
    static const std::vector<std::string_view> commands{
        "",           "view.fit",  "view.home",  "view.front", "view.top",     "view.right",    "sketch.lookAt",
        kTouchMenu,   "edit.undo", "edit.redo",  "edit.confirm", "edit.escape", "tools.measure", "view.section",
    };
    return commands;
}

const char* ModifierName(ModifierChoice m) {
    switch (m) {
#if defined(__APPLE__)
        case ModifierChoice::Ctrl: return "Cmd";
        case ModifierChoice::Alt:  return "Option";
#else
        case ModifierChoice::Ctrl: return "Ctrl";
        case ModifierChoice::Alt:  return "Alt";
#endif
        case ModifierChoice::Shift: return "Shift";
        case ModifierChoice::None:  return "";
    }
    return "";
}

} // namespace bz::app
