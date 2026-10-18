// A tool's choice between a few ways (operation, direction, depth, how a
// rectangle is drawn, the line type). The tool describes it once; where it is
// drawn depends on the device (the author, 18.10.2026):
//  - the desktop: a row of icon buttons in the tool panel, caption on the left
//    (as in KOMPAS-3D); without icons — the usual segmented text strip;
//  - the phone: nothing in block 1 — the choices go to the top island over the
//    view (mobile/MobileLayout.cpp), the line type to an island of its own.
#pragma once

#include <span>

namespace bz::app {

struct AppContext;

struct ChoiceItem {
    const char* icon;    ///< nullptr: shown as text
    const char* label;   ///< the name (a tooltip under an icon); passed through Tr()
};

/// Returns the item chosen now (`current` when nothing changed).
/// `disabledMask`: bit i set — item i cannot be chosen at the moment.
int ToolChoice(AppContext& app, const char* id, const char* title, std::span<const ChoiceItem> items, int current, float width,
               int disabledMask = 0, bool lineType = false);

/// The phone (the author, 18.10.2026: block 1 must not waste room): a tool panel's explanations
/// go to its bottom, the controls stay together at the top. True when `text` was kept for that.
bool DeferCaption(AppContext& app, const char* text);
/// Draws the kept explanations (small, muted, under a thin line) and forgets them.
void FlushCaptions(AppContext& app, float width);

} // namespace bz::app
