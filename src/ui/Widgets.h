#pragma once
// The design system's controls, drawn to match the mockups. All sizes are
// design pixels and get scaled by Ui::scale.

#include "ui/Ui.h"

#include <span>
#include <string>
#include <string_view>

namespace bz::ui {

/// Square icon button (quick access toolbar, left rail, panel headers).
/// `active` draws the selected state of a rail tab.
bool IconButton(Ui& ui, const char* id, std::string_view icon, float size, float iconSize,
                const char* tooltip = nullptr, bool enabled = true, bool active = false);

/// Large ribbon button: 32 px icon over a one- or two-line caption.
bool RibbonLargeButton(Ui& ui, const char* id, std::string_view icon, const char* caption,
                       const char* tooltip, bool enabled = true, bool active = false);

/// Small ribbon button: 20 px icon and a caption on one 24 px row.
bool RibbonSmallButton(Ui& ui, const char* id, std::string_view icon, const char* caption,
                       const char* tooltip, bool enabled = true, bool active = false);

/// Segmented control `width` wide, laid out in `columns` columns. Returns
/// the chosen index (unchanged when nothing was clicked). Bit i of
/// `disabledMask` greys out segment i.
int Segmented(Ui& ui, const char* id, std::span<const char* const> labels, int current, int columns, float width,
              int disabledMask = 0);

/// Horizontal slider for a value in 0..1, `width` wide. Returns true when
/// the value changed this frame (while dragging or on a click on the track).
bool Slider(Ui& ui, const char* id, float& value, float width);

/// Filled accent button. `hint` (e.g. "Enter") is drawn small after the label.
bool PrimaryButton(Ui& ui, const char* id, const char* label, const char* hint, ImVec2 size, bool enabled = true);
/// Outlined neutral button.
bool SecondaryButton(Ui& ui, const char* id, const char* label, const char* hint, ImVec2 size, bool enabled = true);

/// Text-only button in accent colour ("+ Добавить").
bool LinkButton(Ui& ui, const char* id, const char* label);

struct FieldResult {
    bool edited    = false;   ///< text changed this frame
    bool committed = false;   ///< Enter pressed or focus left after an edit
    bool submitted = false;   ///< Enter pressed: the user means "done"
    bool cancelled = false;   ///< Esc pressed in the field: the user means "cancel"
    bool fxClicked = false;   ///< the "fx" button was pressed
    bool active    = false;   ///< the field has keyboard focus
};

/// Numeric field that accepts expressions: value, unit suffix and an "fx"
/// button that links the value to a variable. Shows `error` below in red.
FieldResult ExpressionField(Ui& ui, const char* id, std::string& text, std::string_view unit,
                            const std::string& error, float width, bool focusNow = false, bool showFx = true,
                            bool caretAtEnd = false);

/// Single-line text field in the house style.
bool TextField(Ui& ui, const char* id, std::string& text, float width, const char* placeholder = nullptr,
               bool focusNow = false);

/// Scratch buffer of the one text input being edited (owned by the app).
struct EditState {
    ImGuiID     owner = 0;
    std::string text;
};

/// Text input showing `value`. Returns true once, when the user commits a
/// changed text (Enter or clicking away); the new text is in `state.text`.
/// Escape cancels. `mono` uses the number font.
bool CommitInput(Ui& ui, const char* id, const std::string& value, EditState& state, float width, bool mono,
                 const char* hint = nullptr, bool focusNow = false);

/// A field of a tool that picks several things ("Элемент 1: вершина"). A
/// click on it makes it the one the next pick fills (`active` draws it so);
/// the cross at its right end empties it (`*cleared` set; the cross is shown
/// only when there is a value). An empty `value` shows `waiting` in grey.
/// Returns true when the field itself was clicked.
bool SelectionSlot(Ui& ui, const char* id, const std::string& caption, const std::string& value, const char* waiting, bool active,
                   float width, bool* cleared);

/// Enter, or Enter on the number pad, pressed this frame (with key repeat
/// when `repeat`). The two are one key for the user.
bool EnterPressed(bool repeat = true);

/// Small grey caption above a control ("Операция", "Расстояние").
void FieldLabel(Ui& ui, const char* text);

/// Shows a styled tooltip for the last item after the usual delay.
void ItemTooltip(Ui& ui, const char* text);

/// Horizontal hairline across the current window at the cursor.
void Divider(Ui& ui, const Color& color);

/// Text truncated with "…" to fit `width`.
void ClippedText(const char* text, float width);

} // namespace bz::ui
