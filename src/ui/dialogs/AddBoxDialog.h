#pragma once
// ============================================================
// AddBoxDialog — диалог создания 3D-примитива «Куб».
// ============================================================

#include <functional>

namespace BreezeCAD { class BreezeDocument; }

namespace BreezeCAD::UI::Dialogs {

struct AddBoxState {
    bool  show = false;
    char  name[128] = "Box";
    float pos[3]  = { 0.0f, 0.0f, 0.0f };
    float size[3] = { 100.0f, 100.0f, 100.0f };
};

struct AddBoxContext {
    AddBoxState*    state = nullptr;
    BreezeDocument* doc   = nullptr;
    std::function<void()> onBoxAdded;   // RefreshView
};

void DrawAddBoxDialog(const AddBoxContext& ctx);

} // namespace BreezeCAD::UI::Dialogs
