#include "ui/Widgets.h"

#include "core/I18n.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstring>

namespace bz::ui {
namespace {

ImVec2 Add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }

/// Background of a flat button for its interaction state.
Color FlatBackground(const Ui& ui, bool hovered, bool held, bool active) {
    if (active) return ui.C().accentSoft;
    if (held) return ui.C().pressed;
    if (hovered) return ui.C().hover;
    return Color::Transparent();
}

/// Splits "Выдав-\nливание" style captions into lines.
int SplitLines(const char* caption, const char* lines[2], std::size_t lengths[2]) {
    const char* nl = std::strchr(caption, '\n');
    lines[0] = caption;
    if (!nl) {
        lengths[0] = std::strlen(caption);
        return 1;
    }
    lengths[0] = static_cast<std::size_t>(nl - caption);
    lines[1] = nl + 1;
    lengths[1] = std::strlen(nl + 1);
    return 2;
}

} // namespace

bool IconButton(Ui& ui, const char* id, std::string_view icon, float size, float iconSize, const char* tooltip,
                bool enabled, bool active) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float s = ui.S(size);
    const bool clicked = ImGui::InvisibleButton(id, {s, s}) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color bg = FlatBackground(ui, hovered, held, active);
    if (bg.a > 0) dl->AddRectFilled(pos, Add(pos, {s, s}), ToU32(bg), ui.S(ui.theme.metrics.radiusSmall));
    const Color tint = !enabled ? ui.C().textDisabled : active ? ui.C().accent : ui.C().textSecondary;
    const float is = ui.S(iconSize);
    ui.icons.Draw(dl, icon, Add(pos, {(s - is) / 2, (s - is) / 2}), is, tint);
    if (tooltip) ItemTooltip(ui, tooltip);
    return clicked;
}

bool RibbonLargeButton(Ui& ui, const char* id, std::string_view icon, const char* caption, const char* tooltip,
                       bool enabled, bool active) {
    const char* lines[2];
    std::size_t lengths[2];
    const int count = SplitLines(caption, lines, lengths);

    ImFont* font = ui.fonts.medium;
    const float lineHeight = ui.S(14);
    const float width = ui.S(72);
    const float height = ui.S(6 + 32 + 4) + lineHeight * 2 + ui.S(4);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, {width, height}) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color bg = FlatBackground(ui, hovered, held, active);
    if (bg.a > 0) dl->AddRectFilled(pos, Add(pos, {width, height}), ToU32(bg), ui.S(ui.theme.metrics.radius));

    const float iconSize = ui.S(32);
    // Colour icons keep their own colours; monochrome ones take the text colour.
    const Color alpha = ui.C().textSecondary.WithAlpha(enabled ? 1.0f : 0.38f);
    ui.icons.Draw(dl, icon, Add(pos, {(width - iconSize) / 2, ui.S(6)}), iconSize, alpha);

    const Color textColor = enabled ? ui.C().text : ui.C().textDisabled;
    const float fontSize = ui.S(12);
    float y = pos.y + ui.S(6 + 32 + 4);
    for (int i = 0; i < count; ++i) {
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0, lines[i], lines[i] + lengths[i]);
        dl->AddText(font, fontSize, {pos.x + (width - ts.x) / 2, y}, ToU32(textColor), lines[i], lines[i] + lengths[i]);
        y += lineHeight;
    }
    if (tooltip) ItemTooltip(ui, tooltip);
    return clicked;
}

bool RibbonSmallButton(Ui& ui, const char* id, std::string_view icon, const char* caption, const char* tooltip,
                       bool enabled, bool active) {
    ImFont* font = ui.fonts.body;
    const float fontSize = ui.S(12);
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0, caption);
    const float height = ui.S(24);
    const float width = ui.S(4 + 20 + 8 + 8) + ts.x;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, {width, height}) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color bg = FlatBackground(ui, hovered, held, active);
    if (bg.a > 0) dl->AddRectFilled(pos, Add(pos, {width, height}), ToU32(bg), ui.S(ui.theme.metrics.radiusSmall));
    // Colour icons keep their own colours; monochrome ones take the text colour.
    const Color alpha = ui.C().textSecondary.WithAlpha(enabled ? 1.0f : 0.38f);
    ui.icons.Draw(dl, icon, Add(pos, {ui.S(4), ui.S(2)}), ui.S(20), alpha);
    dl->AddText(font, fontSize, {pos.x + ui.S(32), pos.y + (height - ts.y) / 2},
                ToU32(enabled ? ui.C().text : ui.C().textDisabled), caption);
    if (tooltip) ItemTooltip(ui, tooltip);
    return clicked;
}

int Segmented(Ui& ui, const char* id, std::span<const char* const> labels, int current, int columns, float width,
              int disabledMask) {
    ImGui::PushID(id);
    const int count = static_cast<int>(labels.size());
    const int rows = (count + columns - 1) / columns;
    const float pad = ui.S(3), gap = ui.S(4), cellH = ui.S(28);
    const float cellW = (width - pad * 2 - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float height = pad * 2 + cellH * static_cast<float>(rows) + gap * static_cast<float>(rows - 1);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, Add(origin, {width, height}), ToU32(ui.C().segmentTrack), ui.S(ui.theme.metrics.radius));

    int result = current;
    ImFont* font = ui.fonts.medium;
    const float fontSize = ui.S(12.5f);
    for (int i = 0; i < count; ++i) {
        const int row = i / columns, col = i % columns;
        const ImVec2 p = Add(origin, {pad + static_cast<float>(col) * (cellW + gap), pad + static_cast<float>(row) * (cellH + gap)});
        const bool disabled = (disabledMask >> i) & 1;
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", {cellW, cellH}) && !disabled) result = i;
        const bool hovered = !disabled && ImGui::IsItemHovered();
        ImGui::PopID();

        const bool on = i == current;
        if (on) {
            dl->AddRectFilled(Add(p, {0, ui.S(1)}), Add(p, {cellW, cellH + ui.S(1)}), ToU32(Color{0, 0, 0, ui.theme.dark ? 0.35f : 0.08f}),
                              ui.S(6));
            dl->AddRectFilled(p, Add(p, {cellW, cellH}), ToU32(ui.C().segmentActive), ui.S(6));
        } else if (hovered) {
            dl->AddRectFilled(p, Add(p, {cellW, cellH}), ToU32(ui.C().hover.WithAlpha(0.6f)), ui.S(6));
        }
        const Color tc = disabled ? ui.C().textDisabled : on ? ui.C().text : ui.C().textSecondary;
        const char* label = Tr(labels[static_cast<std::size_t>(i)]);   // tables keep the Russian (N_)
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0, label);
        dl->AddText(font, fontSize, {p.x + (cellW - ts.x) / 2, p.y + (cellH - ts.y) / 2}, ToU32(tc), label);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, height});
    ImGui::PopID();
    return result;
}

bool Slider(Ui& ui, const char* id, float& value, float width) {
    const float height = ui.S(28);
    const float knob = ui.S(7);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {width, height});
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;
    // The knob's centre runs between the track ends.
    const float x0 = pos.x + knob, x1 = pos.x + width - knob;
    if (active) {
        const float v = std::clamp((ImGui::GetIO().MousePos.x - x0) / std::max(1.0f, x1 - x0), 0.0f, 1.0f);
        changed = v != value;
        value = v;
    }
    const ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = pos.y + height / 2;
    const float x = x0 + (x1 - x0) * std::clamp(value, 0.0f, 1.0f);
    dl->AddRectFilled({x0, y - ui.S(2)}, {x1, y + ui.S(2)}, ToU32(c.segmentTrack), ui.S(2));
    dl->AddRectFilled({x0, y - ui.S(2)}, {x, y + ui.S(2)}, ToU32(c.accent), ui.S(2));
    dl->AddCircleFilled({x, y}, knob, ToU32(c.segmentActive));
    dl->AddCircle({x, y}, knob, ToU32(active || hovered ? c.accent : c.fieldBorder), 0, ui.S(1.5f));
    return changed;
}

namespace {

bool ButtonBody(Ui& ui, const char* id, const char* label, const char* hint, ImVec2 size, bool enabled, bool primary) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ui.S(ui.theme.metrics.radius);
    const ThemeColors& c = ui.C();

    if (primary) {
        Color bg = !enabled ? c.textDisabled : (hovered || held) ? c.accentHover : c.accent;
        dl->AddRectFilled(pos, Add(pos, size), ToU32(bg), r);
    } else {
        dl->AddRectFilled(pos, Add(pos, size), ToU32(hovered ? c.hover : c.field), r);
        dl->AddRect(pos, Add(pos, size), ToU32(c.fieldBorder), r, 0, ui.S(1));
    }

    ImFont* font = primary ? ui.fonts.strong : ui.fonts.medium;
    const float fs = ui.S(13), hs = ui.S(11);
    const ImVec2 ls = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
    const ImVec2 hsz = hint ? ui.fonts.mono->CalcTextSizeA(hs, FLT_MAX, 0, hint) : ImVec2{0, 0};
    const float total = ls.x + (hint ? ui.S(6) + hsz.x : 0);
    const float x = pos.x + (size.x - total) / 2;
    const Color tc = primary ? c.textOnAccent : (enabled ? c.text : c.textDisabled);
    dl->AddText(font, fs, {x, pos.y + (size.y - ls.y) / 2}, ToU32(tc), label);
    if (hint) {
        const Color hc = primary ? c.textOnAccent.WithAlpha(0.72f) : c.textMuted;
        dl->AddText(ui.fonts.mono, hs, {x + ls.x + ui.S(6), pos.y + (size.y - hsz.y) / 2 + ui.S(0.5f)}, ToU32(hc), hint);
    }
    return clicked;
}

} // namespace

bool PrimaryButton(Ui& ui, const char* id, const char* label, const char* hint, ImVec2 size, bool enabled) {
    return ButtonBody(ui, id, label, hint, size, enabled, true);
}

bool SecondaryButton(Ui& ui, const char* id, const char* label, const char* hint, ImVec2 size, bool enabled) {
    return ButtonBody(ui, id, label, hint, size, enabled, false);
}

bool LinkButton(Ui& ui, const char* id, const char* label) {
    ImFont* font = ui.fonts.medium;
    const float fs = ui.S(12);
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
    const ImVec2 size{ts.x + ui.S(16), ui.S(26)};
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) dl->AddRectFilled(pos, Add(pos, size), ToU32(ui.C().hover), ui.S(6));
    dl->AddText(font, fs, {pos.x + ui.S(8), pos.y + (size.y - ts.y) / 2}, ToU32(ui.C().accent), label);
    return clicked;
}

FieldResult ExpressionField(Ui& ui, const char* id, std::string& text, std::string_view unit, const std::string& error,
                            float width, bool focusNow, bool showFx, bool caretAtEnd) {
    FieldResult result;
    ImGui::PushID(id);
    const ThemeColors& c = ui.C();
    const float h = ui.S(ui.theme.metrics.fieldHeight);
    const float r = ui.S(ui.theme.metrics.radius);
    const float fxW = showFx ? ui.S(34) : 0.0f;
    ImFont* unitFont = ui.fonts.bodySmall;
    const float unitFs = ui.S(12);
    const std::string unitText(unit);
    const float unitW = unit.empty() ? 0.0f : unitFont->CalcTextSizeA(unitFs, FLT_MAX, 0, unitText.c_str()).x + ui.S(16);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, Add(pos, {width, h}), ToU32(c.field), r);

    // The text input itself: frameless, filling the left part.
    ImGui::PushFont(ui.fonts.monoLarge);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui.S(10), (h - ImGui::GetFontSize()) / 2});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0, 0, 0, 0});
    if (focusNow) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(width - fxW - unitW);
    // caretAtEnd: the text was just added to (a name inserted), keep typing after it.
    const auto toEnd = [](ImGuiInputTextCallbackData* data) {
        data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
        return 0;
    };
    result.edited = caretAtEnd ? ImGui::InputText("##value", &text, ImGuiInputTextFlags_CallbackAlways, toEnd)
                               : ImGui::InputText("##value", &text, ImGuiInputTextFlags_AutoSelectAll);
    result.active = ImGui::IsItemActive();
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    result.submitted = enter && (result.active || ImGui::IsItemDeactivated());
    result.cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && (result.active || ImGui::IsItemDeactivated());
    result.committed = ImGui::IsItemDeactivatedAfterEdit() || result.submitted;
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();

    if (!unit.empty())
        dl->AddText(unitFont, unitFs, {pos.x + width - fxW - unitW + ui.S(8), pos.y + (h - unitFs) / 2 - ui.S(1)},
                    ToU32(c.textMuted), unitText.c_str());

    if (showFx) {
        const ImVec2 fxPos{pos.x + width - fxW, pos.y};
        ImGui::SetCursorScreenPos(fxPos);
        result.fxClicked = ImGui::InvisibleButton("fx", {fxW, h});
        const bool fxHover = ImGui::IsItemHovered();
        dl->AddRectFilled(fxPos, Add(fxPos, {fxW, h}), ToU32(fxHover ? c.hover : c.appBackground), r,
                          ImDrawFlags_RoundCornersRight);
        dl->AddLine(fxPos, {fxPos.x, fxPos.y + h}, ToU32(c.divider), ui.S(1));
        ui.icons.Draw(dl, "ui-fx", {fxPos.x + (fxW - ui.S(16)) / 2, fxPos.y + (h - ui.S(16)) / 2}, ui.S(16), c.textSecondary);
        ItemTooltip(ui, Tr("Связать с переменной"));
    }

    // Border: accent with a soft glow while editing, red on error.
    const Color border = !error.empty() ? c.danger : result.active ? c.accent : c.fieldBorder;
    if (result.active) dl->AddRect(Add(pos, {-ui.S(2), -ui.S(2)}), Add(pos, {width + ui.S(2), h + ui.S(2)}),
                                   ToU32(c.accent.WithAlpha(0.18f)), r + ui.S(2), 0, ui.S(3));
    dl->AddRect(pos, Add(pos, {width, h}), ToU32(border), r, 0, ui.S(1));

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({width, h});
    if (!error.empty()) {
        ImGui::PushFont(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(c.danger));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(Tr(error).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::PopID();
    return result;
}

bool TextField(Ui& ui, const char* id, std::string& text, float width, const char* placeholder, bool focusNow) {
    const ThemeColors& c = ui.C();
    const float h = ui.S(30);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, Add(pos, {width, h}), ToU32(c.field), ui.S(6));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui.S(10), (h - ImGui::GetFontSize()) / 2});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0, 0, 0, 0});
    if (focusNow) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(width);
    const bool changed = placeholder ? ImGui::InputTextWithHint(id, placeholder, &text) : ImGui::InputText(id, &text);
    const bool active = ImGui::IsItemActive();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    dl->AddRect(pos, Add(pos, {width, h}), ToU32(active ? c.accent : c.fieldBorder), ui.S(6), 0, ui.S(1));
    return changed;
}

bool CommitInput(Ui& ui, const char* id, const std::string& value, EditState& state, float width, bool mono,
                 const char* hint, bool focusNow) {
    const ImGuiID gid = ImGui::GetID(id);
    const bool owned = state.owner == gid;
    std::string local = value;
    std::string& buffer = owned ? state.text : local;

    const ThemeColors& c = ui.C();
    const float h = ui.S(28);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(mono ? ui.fonts.mono : ui.fonts.body);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui.S(8), (h - ImGui::GetFontSize()) / 2});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ToVec4(c.hoverRow));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ToVec4(c.field));
    if (focusNow) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(width);
    // Starting to edit selects the whole value: typing replaces it, as in
    // Inventor's parameter table.
    if (hint) ImGui::InputTextWithHint(id, hint, &buffer, ImGuiInputTextFlags_AutoSelectAll);
    else ImGui::InputText(id, &buffer, ImGuiInputTextFlags_AutoSelectAll);
    const bool activated = ImGui::IsItemActivated();
    const bool active = ImGui::IsItemActive();
    const bool deactivated = ImGui::IsItemDeactivated();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    if (active) dl->AddRect(pos, {pos.x + width, pos.y + h}, ToU32(c.accent), ui.S(5), 0, ui.S(1));

    if (activated) {
        state.owner = gid;
        state.text = buffer;
    }
    if (deactivated && state.owner == gid) {
        state.owner = 0;
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
        return !cancelled && state.text != value;
    }
    return false;
}

void FieldLabel(Ui& ui, const char* text) {
    FontScope font(ui.fonts.bodySmall);
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(ui.C().textSecondary));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void ItemTooltip(Ui& ui, const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ui.S(9), ui.S(6)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ui.S(6));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ToVec4(ui.C().tooltip));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(ui.C().tooltipText));
    ImGui::PushStyleColor(ImGuiCol_Border, ToVec4(ui.C().divider));
    FontScope font(ui.fonts.bodySmall);   // before Begin: must outlive the End call
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ui.S(320));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

void Divider(Ui& ui, const Color& color) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float x0 = ImGui::GetWindowPos().x;
    const float x1 = x0 + ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddLine({x0, pos.y}, {x1, pos.y}, ToU32(color), ui.S(1));
    ImGui::Dummy({0, ui.S(1)});
}

void ClippedText(const char* text, float width) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, {pos.x + width, pos.y + h}, pos.x + width,
                              pos.x + width, text, nullptr, nullptr);
    ImGui::Dummy({width, h});
}

} // namespace bz::ui
