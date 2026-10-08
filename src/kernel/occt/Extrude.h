#pragma once

#include "core/Expected.h"
#include "document/ExtrudeFeature.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace bz::kernel::occt {

/// The solid swept by an extrusion of `faces` (before any boolean).
/// `throughAllLength` is used for ExtentType::ThroughAll.
Expected<TopoDS_Shape> MakeExtrusion(const std::vector<TopoDS_Face>& faces,
                                     const doc::ExtrudeFeature& feature,
                                     const Frame& sketchFrame,
                                     double throughAllLength);

/// Applies a boolean of `tool` to `target`. Returns the new body shape.
Expected<TopoDS_Shape> ApplyBoolean(const TopoDS_Shape& target, const TopoDS_Shape& tool, doc::BooleanOp op);

/// Merges coplanar faces and collinear edges left by booleans, so the model
/// keeps a clean topology (one face per flat side).
TopoDS_Shape Clean(const TopoDS_Shape& shape);

} // namespace bz::kernel::occt
