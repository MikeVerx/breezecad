#pragma once
// ============================================================
// ThemeManager — оркестратор применения темы.
//
//   ApplyTheme(theme, outTokens, view)
//
// 1) вызывает Setup* (стиль ImGui)
// 2) заполняет outTokens (наши токены)
// 3) если view не nullptr — задаёт градиент вьюпорта
//
// theme: 0 = Светлая, 1 = Тёмная, 2 = Авто, 3 = legacy Dark.
//
// Реализация — на шаге 2.4 рефакторинга.
// ============================================================

#include "imgui.h"

namespace BreezeCAD {
class OcctView;
struct ThemeTokens;
}

namespace BreezeCAD::UI::Theme {

void ApplyTheme(int theme, ThemeTokens& outTokens, OcctView* view);

} // namespace BreezeCAD::UI::Theme
