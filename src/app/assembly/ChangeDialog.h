#pragma once
// The window «Обнаружено изменение, затрагивающее другие детали» (ADR-0009,
// docs/dev/CHANGE-DIALOG-PLAN.md, mockups docs/dev/mockups/change-dialog-*.png).
//
// Shown over the assembly once the edit of a part is over and a part built
// from it (its sketches project the edited part's sketch) waits for a choice
// (AssemblyState::changes). Modal and with no ✕: the choice is required. A
// choice is made per part; the last part's button applies them all.

#include "app/AppContext.h"

namespace bz::app {

/// Draws the window when the active assembly has changes waiting, and leads
/// «Ручное редактирование» through its sketches one by one.
void DrawChangeDialog(AppContext& app);

} // namespace bz::app
