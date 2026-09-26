#pragma once
// ============================================================
// UnsavedChangesDialog — «Сохранить перед закрытием?»
// ============================================================

#include <functional>

namespace BreezeCAD::UI::Dialogs {

struct UnsavedState {
    bool show = false;
    int  pendingCloseDocIndex = -1;   // какой док закрываем (-1 = не закрывать)
    bool pendingWindowClose   = false;
    char info[256] = "";
};

struct UnsavedContext {
    UnsavedState* state = nullptr;
    int           documentCount = 0;

    std::function<bool(int idx)> onSave;      // попытка сохранить док idx, true = ок
    std::function<void(int idx)> onDiscard;   // пометить как несохранённый
    std::function<void(int idx)> onCloseDoc;  // закрыть док idx
    std::function<void()>        onQuit;      // закрыть приложение
};

void DrawUnsavedChangesDialog(const UnsavedContext& ctx);

} // namespace BreezeCAD::UI::Dialogs
