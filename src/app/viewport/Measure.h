#pragma once
// "Измерить": click entities of the model to see their sizes and the
// distance between two. A card in the viewport shows the numbers; the
// shortest distance is drawn as a line.

#include "app/AppContext.h"

namespace bz::app {

/// Picking and the card while measuring is on (Model mode, no operation open).
void DrawMeasure(AppContext& app, Session& session, const ViewportFrame& vf);

} // namespace bz::app
