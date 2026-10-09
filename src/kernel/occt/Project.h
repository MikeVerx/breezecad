#pragma once

#include "core/Expected.h"
#include "kernel/Projection.h"

#include <TopoDS_Edge.hxx>

namespace bz::kernel::occt {

/// `edge` projected onto the plane of `frame`, in its coordinates. Lines of
/// any direction, and circles and arcs that lie parallel to the plane; other
/// edges (an ellipse in projection, splines) are reported as errors.
Expected<ProjectedCurve> ProjectEdge(const TopoDS_Edge& edge, const Frame& frame);

} // namespace bz::kernel::occt
