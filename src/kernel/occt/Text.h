#pragma once
// OCCT side of sketch text: the letters as planar faces in the XY plane,
// laid out in a box with its bottom-left corner at the origin.

#include "kernel/TextGeometry.h"

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// The faces of the letters; a null shape without a font or for blank text.
TopoDS_Shape TextFaces(const TextFormat& format, double boxWidth, double boxHeight);

} // namespace bz::kernel::occt
