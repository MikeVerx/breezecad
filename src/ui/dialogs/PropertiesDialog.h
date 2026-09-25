#pragma once
// ============================================================
// PropertiesDialog — окно «Свойства» выделенного объекта.
// ============================================================

#include <TDF_Label.hxx>

namespace BreezeCAD { class BreezeDocument; }

namespace BreezeCAD::UI::Dialogs {

struct PropertiesState {
    bool      show = false;
    TDF_Label pendingLabel;
};

struct PropertiesContext {
    PropertiesState* state = nullptr;
    BreezeDocument*  doc   = nullptr;
};

void DrawPropertiesDialog(const PropertiesContext& ctx);

} // namespace BreezeCAD::UI::Dialogs
