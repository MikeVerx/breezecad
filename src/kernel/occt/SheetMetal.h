#pragma once
// The only OCCT side of sheet metal: reading the sketch region into the
// module's 2D outline, and building the folded body from a solved layout
// with plain prisms and revolutions. Everything else (rules, flange layout,
// flat pattern, DXF) lives in src/sheetmetal without OCCT
// (docs/ADR/0007-sheet-metal.md).

#include "core/Expected.h"
#include "core/Math.h"
#include "sheetmetal/SheetPart.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace bz::kernel::occt {

/// A planar sketch region as outer loop and holes in `frame`'s coordinates.
/// Lines and circular arcs stay exact; other curves become short lines.
Expected<sheet::Region> RegionFromFace(const TopoDS_Face& face, const Frame& frame);

/// The folded body: the trimmed base plate plus a bend (part of a tube) and
/// a flat strip for every flange, in model space.
/// `clashes` (when given) gets the flanges whose bend or strip goes into what
/// was built before them (another flange, the plate): the body is still made.
Expected<TopoDS_Shape> MakeSheetBody(const sheet::Part& part, const sheet::Layout& layout, const Frame& frame,
                                     std::vector<std::size_t>* clashes = nullptr);

} // namespace bz::kernel::occt
