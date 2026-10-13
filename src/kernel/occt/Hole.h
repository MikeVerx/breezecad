#pragma once

#include "core/Expected.h"
#include "document/HoleFeature.h"

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// `body` with the holes of `feature` drilled. The face is found again on
/// `body` by its normal and point (doc::FaceReference).
Expected<TopoDS_Shape> DrillHoles(const TopoDS_Shape& body, const doc::HoleFeature& feature);

/// Moves the centres held on an axis (feature.concentric) to where that axis,
/// found again on `body`, crosses the hole's face now; the references follow
/// the edge or face as it is now. Returns how many axes were not found (their
/// centres stay where they were).
std::size_t FollowAxes(const TopoDS_Shape& body, doc::HoleFeature& feature);

/// The cutting tools of all holes of `feature` on `body`, fused (what
/// DrillHoles cuts out; patterns copy it).
Expected<TopoDS_Shape> HoleTools(const TopoDS_Shape& body, const doc::HoleFeature& feature);

} // namespace bz::kernel::occt
