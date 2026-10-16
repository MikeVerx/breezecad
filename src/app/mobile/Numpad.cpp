// The mini numpad in block 1 (docs/ADR/0004-mobile-touch-ui.md, "Ввод значений"):
// instead of the system keyboard, which would cover half the screen and the
// model. 7 × 4 keys:
//
//     7 8 9 / ( ) ⌫
//     4 5 6 × a b ↑
//     1 2 3 − c d ↓
//     0 . пред + ⌨ ↵↵
//
// a–d are the user's own variables, typed as letters; "пред." puts the value
// last confirmed in a field (a value used again and again, the author's ask);
// ↑ ↓ go to the field before or after; ⌨ the full keyboard (functions, new
// names); ↵ confirms. The line above the keys shows what the field holds:
// the field itself may be scrolled out of sight.
//
// Its keys are not ImGui buttons: a click elsewhere would end the editing of
// the field. The touch input (mobile/TouchInput) takes the fingers on them
// and types (FireNumpadKey).
#include "app/mobile/MobileLayout.h"

#include "core/I18n.h"

#include <imgui_internal.h>

namespace bz::app::mobile {
namespace {

struct KeyDef {
    const char* label;
    int         code;
    int         span = 1;
};

constexpr KeyDef kKeys[4][7] = {
    {{"7", '7'}, {"8", '8'}, {"9", '9'}, {"/", '/'}, {"(", '('}, {")", ')'}, {"", NumpadBackspace}},
    {{"4", '4'}, {"5", '5'}, {"6", '6'}, {"\xC3\x97", '*'}, {"a", 'a'}, {"b", 'b'}, {"\xE2\x86\x91", NumpadPrevField}},
    {{"1", '1'}, {"2", '2'}, {"3", '3'}, {"\xE2\x88\x92", '-'}, {"c", 'c'}, {"d", 'd'}, {"\xE2\x86\x93", NumpadNextField}},
    {{"0", '0'}, {".", '.'}, {"", NumpadPrevious}, {"+", '+'}, {"", NumpadKeyboard}, {"\xE2\x86\xB5", NumpadEnter, 2}, {nullptr, 0}},
};

void Press(ImGuiKey key, bool shift = false, bool ctrl = false) {
    ImGuiIO& io = ImGui::GetIO();
    if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
    if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
    if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
    if (shift) io.AddKeyEvent(ImGuiMod_Shift, false);
}

} // namespace

float NumpadHeight(const ui::Ui& ui) { return ui.S(30 + 4 * 46 + 4); }

void DrawNumpad(AppContext& app, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileState& m = app.shell.mobile;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    m.numpadKeys.clear();

    // What the field holds now.
    const ImGuiContext& g = *GImGui;
    if (g.ActiveId != 0 && g.InputTextState.ID == g.ActiveId)
        m.editingText.assign(g.InputTextState.TextA.Data, static_cast<std::size_t>(std::max(0, g.InputTextState.TextLen)));
    const float lineH = ui.S(30);
    dl->AddRectFilled(pos, {pos.x + size.x, pos.y + lineH}, ui::ToU32(c.field), ui.S(6));
    dl->AddRect(pos, {pos.x + size.x, pos.y + lineH}, ui::ToU32(c.accent), ui.S(6));
    ImGui::PushClipRect(pos, {pos.x + size.x - ui.S(6), pos.y + lineH}, true);
    dl->AddText(ui.fonts.mono, ui.S(15), {pos.x + ui.S(8), pos.y + (lineH - ui.S(15)) / 2}, ui::ToU32(c.text), m.editingText.c_str());
    ImGui::PopClipRect();

    const float top = pos.y + lineH + ui.S(4);
    const float cw = size.x / 7.0f, ch = (size.y - lineH - ui.S(4)) / 4.0f;
    for (int r = 0; r < 4; ++r) {
        for (int k = 0; k < 7; ++k) {
            const KeyDef& key = kKeys[r][k];
            if (!key.label) continue;
            const ImVec2 a{pos.x + static_cast<float>(k) * cw + ui.S(2), top + static_cast<float>(r) * ch + ui.S(2)};
            const ImVec2 b{pos.x + static_cast<float>(k + key.span) * cw - ui.S(2), top + static_cast<float>(r + 1) * ch - ui.S(2)};
            const int index = static_cast<int>(m.numpadKeys.size());
            m.numpadKeys.push_back({a, b, key.code});
            const bool down = m.numpadDown == index;
            const bool special = key.code < 0;
            const bool enter = key.code == NumpadEnter;
            const Color fill = enter ? c.accent : down ? c.pressed : special ? c.segmentTrack : c.field;
            dl->AddRectFilled(a, b, ui::ToU32(fill), ui.S(6));
            if (!enter) dl->AddRect(a, b, ui::ToU32(c.fieldBorder), ui.S(6));
            const ImVec2 mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
            const Color ink = enter ? c.textOnAccent : key.code == NumpadPrevious && m.previousValue.empty() ? c.textDisabled : c.text;
            if (key.code == NumpadBackspace) {
                ui.icons.Draw(dl, "ui-backspace", {mid.x - ui.S(10), mid.y - ui.S(10)}, ui.S(20), ink);
            } else if (key.code == NumpadKeyboard) {
                ui.icons.Draw(dl, "ui-keyboard", {mid.x - ui.S(10), mid.y - ui.S(10)}, ui.S(20), ink);
            } else if (key.code == NumpadEnter) {   // the fonts have no ↵
                ui.icons.Draw(dl, "ui-enter", {mid.x - ui.S(11), mid.y - ui.S(11)}, ui.S(22), ink);
            } else {
                const char* text = key.code == NumpadPrevious ? Tr("пред.") : key.label;
                ImFont* font = key.code == NumpadPrevious ? ui.fonts.bodySmall : ui.fonts.medium;
                const float fs = ui.S(key.code == NumpadPrevious ? 12 : 18);
                const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, text);
                dl->AddText(font, fs, {mid.x - ts.x / 2, mid.y - ts.y / 2}, ui::ToU32(ink), text);
            }
        }
    }
}

void FireNumpadKey(AppContext& app, int code) {
    MobileState& m = app.shell.mobile;
    ImGuiIO& io = ImGui::GetIO();
    switch (code) {
        case NumpadBackspace: Press(ImGuiKey_Backspace); break;
        case NumpadPrevField: Press(ImGuiKey_Tab, true); break;
        case NumpadNextField: Press(ImGuiKey_Tab); break;
        case NumpadKeyboard:  m.fullKeyboard = true; break;
        case NumpadEnter:
            if (!m.editingText.empty()) m.previousValue = m.editingText;
            Press(ImGuiKey_Enter);
            break;
        case NumpadPrevious:
            if (m.previousValue.empty()) break;
            Press(ImGuiKey_A, false, true);   // the field's text selected: typing replaces it
            io.AddInputCharactersUTF8(m.previousValue.c_str());
            break;
        default:
            if (code > 0) io.AddInputCharacter(static_cast<unsigned>(code));
            break;
    }
}

} // namespace bz::app::mobile
