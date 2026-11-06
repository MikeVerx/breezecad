#pragma once
// Projections that follow the profile a model edge was made from, not the
// edge itself (see doc::SketchSource).
//
// An edge of an extrusion's cap lies on the curve of its profile moved along
// the sketch's normal. A fillet, chamfer or shell changes the edge (it gets
// shorter, or is gone) but not the profile: a projection that knows the
// profile curve still finds where the line is.

#include "document/Document.h"
#include "document/EdgeReference.h"
#include "document/SketchFeature.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <optional>

namespace bz::kernel::occt {

/// The profile curve `edge` was made from: a line, circle or arc of a sketch
/// of `doc` before feature number `beforeIndex` that an operation used, with
/// the edge lying on it (parallel to the sketch, moved along its normal).
/// nullopt when no sketch curve explains the edge (a fillet's own edge, the
/// side edge of an extrusion, a revolved rim).
std::optional<doc::SketchSource> FindSketchSource(const TopoDS_Edge& edge, const doc::Document& doc, std::size_t beforeIndex);

/// `edge` lies on the curve of `source`; its offset and range are then
/// written to `source`.
bool LiesOnSource(const TopoDS_Edge& edge, const doc::Document& doc, doc::SketchSource& source);

/// The curve of `source` as an edge of the model: the part of it the edge
/// covered, at its offset. nullopt when the sketch or the curve is gone.
std::optional<TopoDS_Edge> SourceCurveEdge(const doc::Document& doc, const doc::SketchSource& source);

/// Where a projection's edge is now. `resolved` is the edge found by its
/// reference (`edge`), or nullopt. With a source in `doc`, the source wins:
/// an edge of `body` on its curve (nearest the old offset), else the curve
/// itself — a projection with a source is lost only when its curve is
/// deleted. Without one, a source is looked for, so older projections gain
/// it. `edge` and `source` are updated to what was found; `onBody` tells
/// whether the edge is one of `body` (false: the profile curve itself).
std::optional<TopoDS_Edge> FollowProjection(const TopoDS_Shape& body, std::optional<TopoDS_Edge> resolved,
                                            const doc::Document* doc, std::size_t beforeIndex, doc::EdgeReference& edge,
                                            doc::SketchSource& source, bool* onBody = nullptr);

} // namespace bz::kernel::occt
