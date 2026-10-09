#pragma once
// OCCT side of sketch text: the letters as planar faces in the XY plane.

#include <TopoDS_Shape.hxx>

#include <string>

namespace bz::kernel::occt {

/// The faces of the letters; a null shape without a font or for blank text.
TopoDS_Shape TextFaces(const std::string& text, double height);

} // namespace bz::kernel::occt
