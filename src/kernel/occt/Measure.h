#pragma once

#include "kernel/Measure.h"

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// Sizes of a vertex, edge or face; `picked` is where it was clicked.
EntityInfo Describe(const TopoDS_Shape& entity, Vec3 picked);

/// Shortest distance and, where it makes sense, the angle between two entities.
Measurement Measure(const TopoDS_Shape& a, const TopoDS_Shape& b);

} // namespace bz::kernel::occt
