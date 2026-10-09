#pragma once
// Finding a referenced flat face again on a rebuilt body (see doc::FaceReference).

#include "document/FaceReference.h"
#include "document/WorkPlaneFeature.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <optional>

namespace bz::kernel::occt {

/// The flat face of `body` with the reference's outward normal whose plane is
/// nearest the stored point; among faces in that same plane, the one nearest
/// the point. nullopt when no flat face faces that way.
std::optional<TopoDS_Face> ResolveFace(const TopoDS_Shape& body, const doc::FaceReference& ref);

/// doc::PlaneFrame of the referenced face as it is on `body` now (the
/// stored point moved onto the face's current plane); nullopt when the face
/// is not found.
std::optional<Frame> ResolvePlaneFrame(const TopoDS_Shape& body, const doc::FaceReference& ref);

/// Outward unit normal of a flat face (nullopt for a curved one).
std::optional<Vec3> PlanarNormal(const TopoDS_Face& face);

/// The cylindrical face of `body` matching `ref` (parallel axis, nearest),
/// as it is now; nullopt when none is near.
std::optional<doc::CylinderTouch> ResolveCylinder(const TopoDS_Shape& body, const doc::CylinderTouch& ref);

} // namespace bz::kernel::occt
