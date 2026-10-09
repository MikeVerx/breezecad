#pragma once

#include "core/Expected.h"
#include "document/CoilFeature.h"

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// The wound wire of `feature` (before any boolean).
Expected<TopoDS_Shape> MakeCoil(const doc::CoilFeature& feature);

} // namespace bz::kernel::occt
