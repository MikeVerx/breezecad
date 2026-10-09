#pragma once
// Popup menus in the house style: roomy rows with an icon column, the
// shortcut right-aligned and muted, a check mark for on/off items.
//
//     ui::MenuStyle style(ui);                 // before BeginPopup*
//     if (ImGui::BeginPopup("##menu")) {
//         if (ui::MenuItem(ui, "Сохранить", "Ctrl+S", "ui-save")) ...
//         if (ui::BeginSubmenu(ui, "Экспорт", "ui-export")) { ...; ImGui::EndMenu(); }
//         ImGui::EndPopup();
//     }

#include "ui/Ui.h"

#include <string_view>

namespace bz::ui {

/// Padding, spacing, rounding and font of a menu. Create it before the
/// BeginPopup* call: it must outlive the matching EndPopup.
class MenuStyle {
public:
    explicit MenuStyle(Ui& ui);
    ~MenuStyle();
    MenuStyle(const MenuStyle&) = delete;
    MenuStyle& operator=(const MenuStyle&) = delete;

private:
    FontScope myFont;
};

/// One menu row. `icon` may be empty; `checked` draws a check mark in the
/// icon column instead. Returns true when clicked (the menu then closes).
bool MenuItem(Ui& ui, const char* label, const char* shortcut = nullptr, std::string_view icon = {}, bool checked = false,
              bool enabled = true);

/// A row that opens a submenu; close it with ImGui::EndMenu() when this returns true.
bool BeginSubmenu(Ui& ui, const char* label, std::string_view icon = {}, bool enabled = true);

} // namespace bz::ui
