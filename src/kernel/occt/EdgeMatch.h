#pragma once
// Describing edges of a body and finding them again after a rebuild.
// See doc::EdgeReference for the idea.

#include "document/EdgeReference.h"

#include <TopoDS_Edge.hxx>
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

} // namespace bz::kernel::occt
