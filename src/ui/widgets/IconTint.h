#pragma once
// ============================================================
// IconTint — цвет тонирования SVG-иконок.
//
// Возвращает ImGuiCol_Text текущей темы (с опциональным
// приглушением для disabled-состояния). Единая точка, чтобы
// иконки везде следовали теме — ribbon, QAT, дерево, home.
// ============================================================

#include "imgui.h"

namespace BreezeCAD::UI::Widgets {

/// Tint иконки: цвет текста темы. enabled=false — прозрачнее.
ImVec4 IconTintColor(bool enabled);

} // namespace BreezeCAD::UI::Widgets
