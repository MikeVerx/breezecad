#pragma once
// ============================================================
// RightPanel — правая панель навигации (свёрнутая / развёрнутая).
//
// Свёрнутая: 40 px + хлястик.
// Развёрнутая: 50 px, 5 кнопок (панорама / зум / орбита / вид / стиль).
// ============================================================

#include <functional>

namespace BreezeCAD { class IconManager; class OcctView; }
namespace BreezeCAD::UI::Widgets { class TooltipState; }

namespace BreezeCAD::UI::Panels {

struct RightPanelContext {
    bool*        expanded        = nullptr;
    bool         isolationActive = false;
    IconManager* icons           = nullptr;
    OcctView*    view            = nullptr;
    UI::Widgets::TooltipState* tooltip = nullptr;

    std::function<void()> onShowAll;   // снять изоляцию + FitAll
};

void DrawRightPanel(const RightPanelContext& ctx);

} // namespace BreezeCAD::UI::Panels
