#pragma once
// Describing edges of a body and finding them again after a rebuild.
// See doc::EdgeReference for the idea.

#include "document/EdgeReference.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Shape.hxx>

#include <optional>
#include <vector>

namespace bz::kernel::occt {

/// What `edge` of `body` looks like.
doc::EdgeReference DescribeEdge(const TopoDS_Edge& edge, const TopoDS_Shape& body);

/// For each reference, the matching edge of `body`, or nullopt when nothing
/// resembles it any more.
std::vector<std::optional<TopoDS_Edge>> ResolveEdges(const std::vector<doc::EdgeReference>& refs,
                                                     const TopoDS_Shape& body);

/// A straight edge as it is on `body` now: a point in its middle and its
/// unit direction. nullopt when it is gone or not straight.
std::optional<std::pair<Vec3, Vec3>> ResolveLine(const doc::EdgeReference& ref, const TopoDS_Shape& body);

/// The end of the edge `ref` on `body` now that was the vertex at `was`:
/// the end that sits where `was` sat relative to the edge's middle (so a
/// moved or resized edge keeps the same end, not the one that happens to be
/// nearer the old place). `ref` is updated to the edge as it is now, to keep
/// it and the new place a pair for the next time. nullopt when the edge is gone.
std::optional<Vec3> ResolveVertex(doc::EdgeReference& ref, const Vec3& was, const TopoDS_Shape& body);

/// An edge of `body` that ends at `vertex`, described; nullopt for a lone vertex.
std::optional<doc::EdgeReference> DescribeVertexEdge(const TopoDS_Vertex& vertex, const TopoDS_Shape& body);

} // namespace bz::kernel::occt
