#pragma once
// A work plane, built one of three ways from planes (origin planes, work
// planes before it) or flat faces of the body:
//   Offset    the start moved along its normal;
//   Midplane  half way between two parallel starts;
//   Angle     through a straight edge, turned from the start by an angle
//             (the edge must lie parallel to the start);
//   ThreePoints through three vertices of the body (kept where they were
//             picked: the plane does not follow later changes of the part);
//   Tangent   touching a cylindrical face where it was clicked.
// Sketches placed on it follow it when it moves.

#include "document/EdgeReference.h"
#include "document/FaceReference.h"
#include "document/Feature.h"

#include <optional>
#include <vector>

namespace bz::doc {

enum class WorkPlaneMethod : unsigned char { Offset, Midplane, Angle, ThreePoints, Tangent };

/// A cylindrical face and a side of it, kept by its shape: found again on
/// the rebuilt body as the cylinder with a parallel axis nearest this one.
struct CylinderTouch {
    Vec3   axisPoint, axisDir;
    double radius = 0;
    Vec3   radial;   ///< from the axis toward where the plane touches (unit, across the axis)
};

class WorkPlaneFeature final : public FeatureBase<WorkPlaneFeature> {
public:
    static constexpr std::string_view kType = "workplane";

    /// The plane it starts from: an origin plane or a work plane before it.
    /// Unused when `face` is set.
    ObjectId      base;
    FaceReference face;          ///< a flat face of the body as the start
    bool          reversed = false;   ///< offset against the base's normal
    Parameter offset{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};

    WorkPlaneMethod method = WorkPlaneMethod::Offset;
    /// Midplane: the second start (like base / face).
    ObjectId      base2;
    FaceReference face2;
    /// Angle: the edge it turns about, and by how much (reversed: the other way).
    std::optional<EdgeReference> edge;
    Parameter angle{.name = "", .expression = "90", .quantity = Quantity::Angle, .value = kPi / 2, .error = {}};
    /// ThreePoints: the picked vertices, model space (up to three).
    std::vector<Vec3> points;
    /// Tangent: the cylinder.
    std::optional<CylinderTouch> cylinder;

    /// Where the plane is after the last rebuild (runtime, never saved).
    std::optional<Frame> frame;

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void CollectParameters(std::vector<Parameter*>& out) override {
        out.push_back(&offset);
        out.push_back(&angle);
    }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// `base` moved along its normal by `distance`; sketch axes keep their directions.
Frame OffsetFrame(const Frame& base, double distance);

} // namespace bz::doc
