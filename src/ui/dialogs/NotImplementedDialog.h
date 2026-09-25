#pragma once
// ============================================================
// NotImplementedDialog — модалка «Функция не разработана».
// ============================================================

#include <string>

namespace BreezeCAD::UI::Dialogs {

struct NotImplementedState {
    bool        show = false;
    std::string feature;
};

void DrawNotImplementedDialog(NotImplementedState& state);

} // namespace BreezeCAD::UI::Dialogs
