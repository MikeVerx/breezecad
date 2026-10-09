#pragma once

#include "core/Expected.h"

#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>

#include <vector>

namespace bz::kernel::occt {

/// A solid through closed `outlines` in order; `ruled`: flat between them.
Expected<TopoDS_Shape> MakeLoft(const std::vector<TopoDS_Wire>& outlines, bool ruled);

} // namespace bz::kernel::occt
