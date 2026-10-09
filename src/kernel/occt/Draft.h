#pragma once

#include "core/Expected.h"
#include "document/DraftFeature.h"

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// `body` with the faces of `feature` tilted about the line where they meet
/// `neutral` (its normal, or the opposite one, is the pull direction).
Expected<TopoDS_Shape> MakeDraft(const TopoDS_Shape& body, const doc::DraftFeature& feature, const Frame& neutral);

} // namespace bz::kernel::occt
