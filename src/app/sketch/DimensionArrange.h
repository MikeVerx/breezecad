#pragma once
// Where to put a dimension's text so it does not sit on other texts, on the
// sketch's lines or on other dimensions' lines. The phone places new
// dimensions with it (a finger cannot nudge a label the way a mouse can);
// "Упорядочить размеры" lays out a whole sketch with it, on any device.

#include "core/Units.h"
#include "sketch/Sketch.h"

namespace bz::app {

/// Screen terms of the layout: the size of a label and the gaps are in pixels
/// of the current view, turned into sketch units with `mmPerPixel`.
struct ArrangeScale {
    double mmPerPixel = 0.1;   ///< sketch units per screen pixel
    double uiScale = 1.0;      ///< the interface scale (labels are drawn at 12 px × it)
};

/// Sets `c.labelOffset` (c need not be in the sketch yet). The side and the
/// direction it has now are kept as the wish: the text moves out along it,
/// turns a little or goes to the other side only when that is crowded.
/// Every dimension of `sk` other than `c` counts as an obstacle.
void ArrangeDimension(const sketch::Sketch& sk, sketch::Constraint& c, const UnitSystem& units, const ArrangeScale& scale);

/// Lays out every dimension of the sketch, nearest to the middle first, each
/// against the geometry and the ones already placed. Returns how many moved.
int ArrangeDimensions(sketch::Sketch& sk, const UnitSystem& units, const ArrangeScale& scale);

} // namespace bz::app
