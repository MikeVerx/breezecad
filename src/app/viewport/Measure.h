#pragma once
// "Измерить": click entities of the model to see their sizes and the
// distance between two. The left panel shows two fields ("Элемент 1",
// "Элемент 2": a click on a field makes it the one the next pick fills, its
// cross empties it) and every number with "Копировать"; on the model there
// is only the measuring line and a short label. A click on empty space
// changes nothing — the tool stays on until Esc or the cross.

#include "app/AppContext.h"

namespace bz::app {

/// Picking, the line and its label while measuring is on (Model mode, no operation open).
void DrawMeasure(AppContext& app, Session& session, const ViewportFrame& vf);

/// The left panel while measuring is on.
void DrawMeasurePanel(AppContext& app, Session& session, float width);

} // namespace bz::app
