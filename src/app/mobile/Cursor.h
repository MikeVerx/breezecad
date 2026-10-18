#pragma once
// The drop cursor (docs/ADR/0004-mobile-touch-ui.md, "Курсор-капля").
//
// A round part under the finger and a tip a little above it, marked in a
// contrasting colour: the finger never covers the point it works on. The
// magnifier at the tip was removed at the author's word (17.10.2026): the
// tip and the snaps are enough.

#include "app/AppContext.h"

namespace bz::app::mobile {

/// Draws the cursor in the view (its draw list), when it is in use.
void DrawDropCursor(AppContext& app, ImDrawList* dl);

} // namespace bz::app::mobile
