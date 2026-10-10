#pragma once
// Characteristic points of edges and faces (for work points).

#include "core/Math.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>

#include <optional>

namespace bz::kernel::occt {

/// Centre of a circle, arc or ellipse edge; nullopt for other curves.
std::optional<Vec3> EdgeCenter(const TopoDS_Edge& edge);

/// The point half way along the edge.
Vec3 EdgeMiddle(const TopoDS_Edge& edge);

/// Centre of a spherical face; nullopt for other surfaces.
std::optional<Vec3> SphereCenter(const TopoDS_Face& face);

} // namespace bz::kernel::occt
