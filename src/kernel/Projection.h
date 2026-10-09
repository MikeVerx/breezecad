#pragma once
// A model edge as it appears on a sketch plane ("Проецировать геометрию").
// OCCT-free: the sketch editor makes sketch curves from it.

#include "core/Math.h"

namespace bz::kernel {

struct ProjectedCurve {
    enum class Kind : unsigned char { Line, Circle, Arc } kind = Kind::Line;
    Vec2   start, end;      ///< line ends; arc ends, counter-clockwise on the sketch plane
    Vec2   centre;          ///< circles and arcs
    double radius = 0.0;
};

} // namespace bz::kernel
