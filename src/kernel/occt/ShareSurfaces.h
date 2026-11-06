#pragma once
// Coaxial cylinders of one radius made to share one surface before faces are
// merged (see Clean in Extrude.cpp).
//
// A fillet's quarter-cylinder and an extrusion of the projected arc of that
// fillet are one cylinder, but OCCT gives each its own frame: the angle is
// counted from another direction. ShapeUpgrade_UnifySameDomain (OCCT 7.8)
// then never finishes on some such pairs — a part stacked from extrusions of
// a filleted outline hung the program — and the faces stay apart, with a
// line between them that no fillet can cross. Given one surface, they are
// plainly the same: merged at once.

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// `shape` with every group of cylindrical faces on one axis and radius
/// using one surface (the first face's), their edges' curves on it moved
/// to match. Faces that close on themselves are left alone. `shape` itself
/// when there is nothing to share or OCCT refuses.
TopoDS_Shape ShareCylinders(const TopoDS_Shape& shape);

} // namespace bz::kernel::occt
