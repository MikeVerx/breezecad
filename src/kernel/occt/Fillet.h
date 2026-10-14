#pragma once

#include "core/Expected.h"
#include "document/EdgeFeature.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace bz::kernel::occt {

/// `body` with `edges` rounded by `radius`.
Expected<TopoDS_Shape> MakeFillet(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double radius);

/// `body` with `edges` bevelled by `distance` on both faces.
Expected<TopoDS_Shape> MakeChamfer(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double distance);

/// Size of a chamfer (values of doc::ChamferFeature, angle in radians).
struct ChamferSize {
    doc::ChamferMode mode = doc::ChamferMode::EqualDistance;
    double distance = 1, distance2 = 1, angle = kPi / 4;
    bool   flipped = false;
    std::vector<bool> flips;   ///< per edge (same order as `edges`): the other face; missing: no
};
/// `body` with `edges` bevelled. Unequal sizes are measured from each edge's
/// reference face: of its two faces, the one whose outward normal at the
/// edge's middle points more along +Z (then +Y, then +X) — the upper one on
/// a part lying on XY — or the other one when `flipped` (all edges) differs
/// from that edge's `flips` entry. The distance lies
/// on the reference face; the angle is between the bevel and that face.
Expected<TopoDS_Shape> MakeChamfer(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, const ChamferSize& size);

} // namespace bz::kernel::occt
