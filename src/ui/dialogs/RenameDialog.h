#pragma once
// ============================================================
// RenameDialog — модальное окно переименования фичи.
// ============================================================

#include <TDF_Label.hxx>

namespace BreezeCAD { class BreezeDocument; }

namespace BreezeCAD::UI::Dialogs {

struct RenameState {
    bool      show = false;
    TDF_Label pendingLabel;
    char      buffer[128] = "";
};

struct RenameContext {
    RenameState*    state = nullptr;
    BreezeDocument* doc   = nullptr;
};

void DrawRenameDialog(const RenameContext& ctx);

} // namespace BreezeCAD::UI::Dialogs
