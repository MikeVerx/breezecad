#pragma once

#include "core/Expected.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace bz::kernel::occt {

/// `body` with `edges` rounded by `radius`.
Expected<TopoDS_Shape> MakeFillet(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double radius);

/// `body` with `edges` bevelled by `distance` on both faces.
Expected<TopoDS_Shape> MakeChamfer(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double distance);

} // namespace bz::kernel::occt
