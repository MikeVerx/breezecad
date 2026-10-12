#pragma once
// Faces and edges of a model's bodies as a numbered list: for commands that
// name geometry without a mouse (modules, the MCP server). Each entry says
// what it is (a plane, a cylinder; a line, a circle), where and how big, and
// carries the reference an operation stores (doc::FaceReference,
// doc::EdgeReference) — found again on the rebuilt body, as a click is.
//
// The numbers are positions in this list: they hold while the model stays
// as it is and mean nothing after a change. OCCT-free.

#include "core/Id.h"
#include "core/Math.h"
#include "document/EdgeReference.h"
#include "document/FaceReference.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Measure.h"

#include <optional>
#include <vector>

namespace bz::kernel {

class Model;

struct FaceInfo {
    int                   index = 0;
    ObjectId              body;           ///< the feature that made the body
    doc::SurfaceKind      kind = doc::SurfaceKind::Other;
    doc::FaceReference    ref;            ///< curved faces: ref.curved
    Vec3                  point;          ///< a point on the face (ref.point)
    Vec3                  normal;         ///< outward there, unit
    double                area = 0;       ///< mm²
    Vec3                  boxMin, boxMax; ///< tight box, mm
    /// Cylinders, cones, spheres, tori: the axis (a point, unit direction)
    /// and the radius (cone: at `point`; torus: the tube's).
    std::optional<doc::CylinderTouch> axis;
    bool                  hole = false;   ///< a cylinder whose material is outside (a bore, not a shaft)
    std::vector<int>      edges;          ///< indices into Topology::edges
};

struct EdgeInfo {
    int                 index = 0;
    ObjectId            body;
    doc::CurveKind      kind = doc::CurveKind::Other;
    doc::EdgeReference  ref;
    double              length = 0;   ///< mm
    Vec3                start, end, middle;
    /// Lines: unit direction; circles: the axis.
    Vec3                direction;
    std::optional<Vec3> centre;       ///< circles and arcs
    double              radius = 0;   ///< circles and arcs
    bool                closed = false;   ///< a full circle (start = end)
    std::vector<int>    faces;        ///< indices into Topology::faces (one or two)
};

struct Topology {
    std::vector<FaceInfo> faces;
    std::vector<EdgeInfo> edges;
};

/// Distance from `p` to the box [lo, hi]; 0 inside it.
double BoxDistance(const Vec3& lo, const Vec3& hi, const Vec3& p);
/// Distance from `p` to an edge: exact for lines and circles (arcs: to the
/// nearer of the arc and its ends), to its three points otherwise.
double EdgeDistance(const EdgeInfo& e, const Vec3& p);

/// Every face and edge of the model's bodies, in a stable order for an
/// unchanged model (body by body, as OCCT lists them).
Topology DescribeTopology(const Model& model);

/// Shortest distance (and the angle between two flat faces or two straight
/// edges) between two entries of DescribeTopology(model): `aIsFace` picks
/// faces or edges. nullopt for indices out of range.
std::optional<Measurement> MeasureBetween(const Model& model, bool aIsFace, int a, bool bIsFace, int b);

} // namespace bz::kernel
