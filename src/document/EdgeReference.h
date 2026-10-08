#pragma once
// A reference to an edge of a body, kept across rebuilds.
//
// Topology is rebuilt from scratch whenever the recipe changes, so an edge
// cannot be stored by index. It is stored by what it looks like: the kind
// and direction of its curve and of its two faces, plus where its middle
// is. After a rebuild the kernel looks for the best match (see
// kernel::occt::ResolveEdge). Directions of faces rarely change when sizes
// change, so the edges of prismatic parts are found reliably; the middle
// point only decides between otherwise equal candidates.

#include "core/Math.h"

#include <array>

namespace bz::doc {

enum class CurveKind : unsigned char { Line, Circle, Other };
enum class SurfaceKind : unsigned char { Plane, Cylinder, Cone, Sphere, Torus, Other };

struct FaceSignature {
    SurfaceKind kind = SurfaceKind::Other;
    Vec3        direction;   ///< outward normal of a plane, axis of a cylinder/cone; zero otherwise
};

struct EdgeReference {
    CurveKind curve = CurveKind::Other;
    Vec3      direction;     ///< line direction or circle axis (unit, sign made canonical)
    double    radius = 0.0;  ///< circles
    Vec3      middle;        ///< point at the middle of the edge, model space
    std::array<FaceSignature, 2> faces;   ///< in a canonical order
};

} // namespace bz::doc
