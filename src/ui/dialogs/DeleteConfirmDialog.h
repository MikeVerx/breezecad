#pragma once
// ============================================================
// DeleteConfirmDialog — подтверждение удаления фичи.
// ============================================================

#include <functional>
#include <TDF_Label.hxx>

namespace BreezeCAD { class BreezeDocument; }

namespace BreezeCAD::UI::Dialogs {

struct DeleteConfirmState {
    bool      show        = false;
    bool      skipConfirm = false;
    char      info[256]   = "";
    TDF_Label pendingLabel;
};

struct DeleteConfirmContext {
    DeleteConfirmState* state = nullptr;
    BreezeDocument*     doc   = nullptr;
    std::function<void()> onDeleted;   // RefreshView + FitAll
};

void DrawDeleteConfirmDialog(const DeleteConfirmContext& ctx);

} // namespace BreezeCAD::UI::Dialogs
