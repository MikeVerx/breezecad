#pragma once

#include "core/Expected.h"
#include "kernel/Projection.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// `edge` projected onto the plane of `frame`, in its coordinates. Lines of
/// any direction; circles and arcs parallel to the plane as they are, seen
/// edge-on as the line they cover (a cylinder's rim from the side); a whole
/// circle at a slant as an ellipse. Other edges (a slanted arc, splines) are
/// reported as errors.
Expected<ProjectedCurve> ProjectEdge(const TopoDS_Edge& edge, const Frame& frame);

/// The body of revolution `face` belongs to (the faces of `body` around the
/// same axis, connected to `face`) seen on the plane of `frame`: the axis,
/// the outline and the cut by the plane. Curves that are neither lines nor
/// circles come as short lines. An error when `face` is not a surface of
/// revolution.
Expected<RevolvedProjection> ProjectRevolved(const TopoDS_Shape& body, const TopoDS_Face& face, const Frame& frame);

} // namespace bz::kernel::occt
