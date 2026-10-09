#pragma once
// Measuring what the user clicks: one entity (a vertex, an edge, a face)
// and the distance and angle between two. OCCT-free, so the UI can show it;
// the computation is in kernel/occt/Measure.h.

#include "core/Math.h"

#include <optional>

namespace bz::kernel {

enum class EntityKind : unsigned char { Vertex, Edge, Face };

/// Sizes of one entity.
struct EntityInfo {
    EntityKind            kind = EntityKind::Face;
    Vec3                  point;      ///< a vertex; a point on the entity otherwise
    std::optional<double> length;     ///< edges, mm
    std::optional<double> radius;     ///< circular edges, cylindrical and spherical faces
    std::optional<double> area;       ///< faces, mm^2
    bool                  straight = false;   ///< a line edge
    bool                  flat = false;       ///< a planar face
};

/// Between two entities.
struct Measurement {
    double                distance = 0.0;   ///< shortest, mm
    Vec3                  from, to;         ///< where the shortest distance is
    std::optional<double> angle;            ///< between two flat faces or two straight edges, radians (0..pi/2)
};

} // namespace bz::kernel
