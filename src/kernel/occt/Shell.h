#pragma once

#include "core/Expected.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace bz::kernel::occt {

/// `body` made hollow with walls `thickness` thick: `open` faces are removed;
/// with none the hollow is closed inside. Walls grow inward unless `outward`.
Expected<TopoDS_Shape> MakeShell(const TopoDS_Shape& body, const std::vector<TopoDS_Face>& open, double thickness, bool outward);

} // namespace bz::kernel::occt
