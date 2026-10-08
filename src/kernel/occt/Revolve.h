#pragma once

#include "core/Expected.h"
#include "document/RevolveFeature.h"
#include "sketch/Sketch.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace bz::kernel::occt {

/// The solid swept by turning `faces` around the feature's axis (before
/// any boolean). `sketch` provides the axis line; `frame` places it.
Expected<TopoDS_Shape> MakeRevolution(const std::vector<TopoDS_Face>& faces, const doc::RevolveFeature& feature,
                                      const sketch::Sketch& sketch, const Frame& frame);

} // namespace bz::kernel::occt
