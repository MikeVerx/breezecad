#pragma once
// ============================================================
// SvgButton — маленькая кнопка с SVG-иконкой Lucide.
//
// Использует IconManager для загрузки текстуры, IconTintColor
// для тонирования. Два варианта: полный (с фоном) и «прозрачный»
// (только hover).
// ============================================================

#include "imgui.h"

namespace BreezeCAD { class IconManager; }

namespace BreezeCAD::UI::Widgets {

/// Кнопка с SVG-иконкой и опциональным tooltip.
/// transparentBg = true — фон не рисуется, только hover.
bool SvgIconBtn(IconManager& icons, const char* id,
                const char* name, int iconSize,
                float btnW, float btnH,
                const char* tooltip = nullptr,
                bool transparentBg = false);

/// QAT-обёртка: 32×28, прозрачный фон.
bool QatIconButton(IconManager& icons, const char* id,
                   const char* name, int size,
                   const char* tooltip = nullptr);

} // namespace BreezeCAD::UI::Widgets
