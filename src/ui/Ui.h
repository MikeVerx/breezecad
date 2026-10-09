#pragma once

#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <imgui.h>

namespace bz::ui {

/// Everything a widget needs to draw itself in the house style.
/// Passed by reference into panels; there is no global UI state.
struct Ui {
    const Theme& theme;
    const Fonts& fonts;
    Icons&       icons;
    float        scale = 1.0f;   ///< UI scale: logical pixels per design pixel

    /// Design pixels -> ImGui coordinates.
    float S(float designPixels) const { return designPixels * scale; }
    ImVec2 S(float x, float y) const { return {x * scale, y * scale}; }

    const ThemeColors& C() const { return theme.colors; }
};

inline ImU32 ToU32(const Color& c) { return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, c.a}); }
inline ImVec4 ToVec4(const Color& c) { return {c.r, c.g, c.b, c.a}; }

/// Pushes a font for the lifetime of the object.
/// Create it BEFORE ImGui::Begin*/BeginPopup*/BeginTooltip, never inside:
/// ImGui requires the font stack to be balanced when the window ends.
class FontScope {
public:
    explicit FontScope(ImFont* font) { ImGui::PushFont(font); }
    ~FontScope() { ImGui::PopFont(); }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

} // namespace bz::ui
