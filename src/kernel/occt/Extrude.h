#pragma once

#include "core/Expected.h"
#include "document/ExtrudeFeature.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <optional>
#include <vector>

namespace bz::kernel::occt {

/// The solid swept by an extrusion of `faces` (before any boolean).
/// `throughAllLength` is used for ExtentType::ThroughAll; `target` is the
/// plane of ExtentType::ToPlane, or `untilFace` a curved face to go up to
/// (found by the caller).
Expected<TopoDS_Shape> MakeExtrusion(const std::vector<TopoDS_Face>& faces,
                                     const doc::ExtrudeFeature& feature,
                                     const Frame& sketchFrame,
                                     double throughAllLength,
                                     const std::optional<Frame>& target = std::nullopt,
                                     const std::optional<TopoDS_Face>& untilFace = std::nullopt);

/// Union of two shapes. Throws Standard_Failure like BRepAlgoAPI_Fuse when OCCT fails.
TopoDS_Shape Fuse(const TopoDS_Shape& a, const TopoDS_Shape& b);

/// Applies a boolean of `tool` to `target`. Returns the new body shape.
Expected<TopoDS_Shape> ApplyBoolean(const TopoDS_Shape& target, const TopoDS_Shape& tool, doc::BooleanOp op);
/// Joins or cuts several tools at once (they may overlap each other), as
/// patterns need. NewBody and Intersect are not accepted here.
Expected<TopoDS_Shape> ApplyBoolean(const TopoDS_Shape& target, const std::vector<TopoDS_Shape>& tools, doc::BooleanOp op);

/// Merges coplanar faces and collinear edges left by booleans, so the model
/// keeps a clean topology (one face per flat side).
TopoDS_Shape Clean(const TopoDS_Shape& shape);

} // namespace bz::kernel::occt
