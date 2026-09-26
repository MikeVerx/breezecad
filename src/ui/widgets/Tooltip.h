#pragma once
// ============================================================
// Tooltip — трёхуровневый tooltip (0.5 / 1.5 / 3.0 сек).
//
// Хранит состояние hovered-id, время старта, уровень.
// Один экземпляр на Application (myTooltipState).
// ============================================================

#include <string>

namespace BreezeCAD::UI::Widgets {

class TooltipState {
public:
    TooltipState()  = default;
    ~TooltipState() = default;

    /// Нарисовать tooltip (3 уровня: 0.5 / 1.5 / 3.0 сек).
    /// Вызывается СРАЗУ после отрисовки виджета — проверяет
    /// ImGui::IsItemHovered() текущего item.
    void Draw(const std::string& id,
              const std::string& name,
              const std::string& shortDesc,
              const std::string& longDesc,
              const std::string& imagePath = "");

private:
    std::string myHoveredItemId;
    double      myHoverStartTime   = 0.0;
    int         myLastTooltipLevel = 0;
};

} // namespace BreezeCAD::UI::Widgets
