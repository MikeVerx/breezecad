#pragma once

#include "core/Expected.h"
#include "document/HoleFeature.h"

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// `body` with the holes of `feature` drilled. The face is found again on
/// `body` by its normal and point (doc::FaceReference).
Expected<TopoDS_Shape> DrillHoles(const TopoDS_Shape& body, const doc::HoleFeature& feature);

/// The plane of the face `ref` points at on `body`: same outward normal,
/// nearest to the stored point. Returns a point on it (nullopt: no such face).
std::optional<Vec3> FindPlanarFace(const TopoDS_Shape& body, const doc::FaceReference& ref);

} // namespace bz::kernel::occt
