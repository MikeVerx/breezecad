#include "IconTint.h"

namespace BreezeCAD::UI::Widgets {

ImVec4 IconTintColor(bool enabled) {
    // Tint иконок = цвет текста темы. В светлой теме — чёрный,
    // в тёмной — светлый. Единообразно, без хардкода.
    ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (!enabled) tint.w *= 0.45f;
    return tint;
}

} // namespace BreezeCAD::UI::Widgets
