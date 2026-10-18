#include "ui/Menus.h"

#include "ui/Widgets.h"

#include <algorithm>

namespace bz::ui {
namespace {

// Rows follow ImGui's menu layout: an item is one text line high and the
// vertical item spacing is split above and below it, so ImGui's own submenu
// rows and ours line up. The highlight covers the whole row.
constexpr float kRow = 28.0f;         ///< row height, design px
constexpr float kIconColumn = 30.0f;  ///< from the row's left edge to the label
constexpr float kMinWidth = 220.0f;

/// Top of the row whose text line starts at `lineTop`.
float RowTop(Ui& ui, float lineTop) { return lineTop - (ui.S(kRow) - ImGui::GetFontSize()) / 2; }

/// Draws a row's icon (or check mark) and label; `top` is the row's top.
void DrawRow(Ui& ui, ImDrawList* dl, ImVec2 top, const char* label, std::string_view icon, bool checked, bool enabled) {
    const ThemeColors& c = ui.C();
    const float height = ui.S(kRow);
    const float iconSize = ui.S(16);
    const ImVec2 iconPos{top.x + ui.S(6), top.y + (height - iconSize) / 2};
    if (checked) ui.icons.Draw(dl, "ui-check", iconPos, iconSize, enabled ? c.accent : c.textDisabled);
    else if (!icon.empty()) ui.icons.Draw(dl, icon, iconPos, iconSize, c.textSecondary.WithAlpha(enabled ? 1.0f : 0.38f));
    dl->AddText(ui.fonts.body, ui.S(13), {top.x + ui.S(kIconColumn), top.y + (height - ui.S(13)) / 2 - ui.S(1)},
                ToU32(enabled ? c.text : c.textDisabled), label);
}

float LabelWidth(Ui& ui, const char* text) { return ui.fonts.body->CalcTextSizeA(ui.S(13), FLT_MAX, 0, text).x; }

} // namespace

MenuStyle::MenuStyle(Ui& ui) : myFont(ui.fonts.body) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(6, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui.S(6), ui.S(kRow) - ImGui::GetFontSize()});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui.S(10));
    ImGui::PushStyleColor(ImGuiCol_Header, ToVec4(ui.C().hoverRow));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ToVec4(ui.C().hoverRow));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ToVec4(ui.C().pressed));
    ImGui::PushStyleColor(ImGuiCol_Separator, ToVec4(ui.C().dividerSoft));
}

MenuStyle::~MenuStyle() {
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
}

bool MenuItem(Ui& ui, const char* label, const char* shortcut, std::string_view icon, bool checked, bool enabled) {
    if (!ui.keyHints) shortcut = nullptr;
    const float shortcutWidth = shortcut ? ui.fonts.mono->CalcTextSizeA(ui.S(11.5f), FLT_MAX, 0, shortcut).x : 0.0f;
    const float width = std::max(ui.S(kMinWidth), ui.S(kIconColumn) + LabelWidth(ui, label) + ui.S(32) + shortcutWidth + ui.S(8));
    const ImVec2 line = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    // The label is reserved as width only; the visible text is drawn below.
    const bool clicked = ImGui::Selectable("##item", false, enabled ? 0 : ImGuiSelectableFlags_Disabled, {width, 0});
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawRow(ui, dl, {line.x, RowTop(ui, line.y)}, label, icon, checked, enabled);
    if (shortcut) {
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ui.S(14);
        dl->AddText(ui.fonts.mono, ui.S(11.5f), {right - shortcutWidth, RowTop(ui, line.y) + (ui.S(kRow) - ui.S(11.5f)) / 2},
                    ToU32(ui.C().textMuted), shortcut);
    }
    return clicked && enabled;
}

bool BeginSubmenu(Ui& ui, const char* label, std::string_view icon, bool enabled) {
    // ImGui's own row opens the submenu and draws the arrow; it gets an
    // invisible label as wide as ours, and ours is drawn over it.
    const ImVec2 line = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float space = std::max(1.0f, ImGui::CalcTextSize(" ").x);
    const auto pad = static_cast<std::size_t>((ui.S(kIconColumn) + LabelWidth(ui, label) + ui.S(28)) / space);
    const std::string reserved = std::string(std::max<std::size_t>(pad, 1), ' ') + "##" + label;
    const bool open = ImGui::BeginMenu(reserved.c_str(), enabled);
    DrawRow(ui, dl, {line.x, RowTop(ui, line.y)}, label, icon, false, enabled);
    return open;
}

} // namespace bz::ui
