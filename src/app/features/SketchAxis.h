#pragma once
// Picking an axis line of a sketch (normally an осевая) in the viewport, for
// operations that turn or wind around it: Вращение, Пружина.

#include "app/AppContext.h"
#include "app/sketch/SketchView.h"
#include "sketch/Sketch.h"

#include <optional>

namespace bz::app {

/// An axis through `a` and `b`, drawn across the whole view.
void DrawSketchAxis(AppContext& app, ImDrawList* dl, const SketchView& view, Vec2 a, Vec2 b, Color color, float width);

/// Highlights the line under the cursor; returns it when clicked.
std::optional<ObjectId> PickSketchLine(AppContext& app, Session& session, const sketch::Sketch& sk, const SketchView& view);

/// The panel chip saying which axis is chosen.
void SketchAxisChip(AppContext& app, const sketch::Line* line, bool legacyAxis, float width);

/// The осевая of a sketch, failing that its first helper line; empty when none.
ObjectId DefaultAxisLine(const sketch::Sketch& sk);

} // namespace bz::app
