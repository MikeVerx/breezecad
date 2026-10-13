#pragma once
// A reference to a flat face of a body, kept across rebuilds.
//
// Like EdgeReference it is stored by what the face looks like, not by index:
// its outward normal and a point that was on it when it was picked. After a
// rebuild the kernel takes the flat face with the same normal whose plane is
// nearest the point (kernel::occt::ResolveFace), so the reference follows the
// face when the part changes size along that normal.

#include "core/Math.h"

namespace bz::doc {

struct FaceReference {
    Vec3 normal;   ///< outward, unit; zero means "not chosen" (for a curved face: at `point`)
    Vec3 point;    ///< a point of the face when it was picked
    bool curved = false;   ///< any face, found again as the one nearest `point` (kernel::occt::ResolveAnyFace)

    bool IsSet() const { return normal.Length() > 0.5; }
    bool operator==(const FaceReference&) const = default;
};

/// Coordinates on the face's plane: origin at the foot of the model origin,
/// x along the model X projected on the plane (Y when the plane faces X).
/// They do not change when the face moves along its normal, so positions
/// measured in them (hole centres) follow the face.
Frame PlaneFrame(const FaceReference& face);

} // namespace bz::doc
