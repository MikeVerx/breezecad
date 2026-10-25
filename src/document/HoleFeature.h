#pragma once
// Holes drilled into a flat face of the existing body.
//
// The face is kept by its outward normal and a point on it, and found again
// on the rebuilt body (the planar face with that normal nearest the point).
// Centres live in the face's plane, in coordinates anchored to the model
// origin (see PlaneFrame in FaceReference.h), so they stay put when the face moves along
// its normal, e.g. when the part gets thicker.

#include "core/Math.h"
#include "document/EdgeReference.h"
#include "document/FaceReference.h"
#include "document/Feature.h"
#include "document/WorkPlaneFeature.h"   // CylinderTouch

#include <optional>
#include <vector>

namespace bz::doc {

enum class HoleType : unsigned char { Simple, Counterbore, Countersink };

/// A centre held on the axis of a round edge or a cylindrical face of the
/// body ("соосно"): on every rebuild it moves to where that axis crosses the
/// hole's face, so the hole stays coaxial when the shaft or boss moves.
struct HoleAxis {
    std::size_t                  centre = 0;   ///< index into HoleFeature::centres
    std::optional<EdgeReference> edge;        ///< a circular edge
    std::optional<CylinderTouch> cylinder;    ///< or a cylindrical face (`radial` unused)
};

class HoleFeature final : public FeatureBase<HoleFeature> {
public:
    static constexpr std::string_view kType = "hole";

    FaceReference     face;
    std::vector<Vec2> centres;      ///< in PlaneFrame(face)
    std::vector<HoleAxis> concentric;   ///< centres held on an axis (optional, 0.2.2)
    HoleType          type = HoleType::Simple;
    bool              throughAll = false;
    Parameter diameter{.name = "", .expression = "6.6", .quantity = Quantity::Length, .value = 6.6, .error = {}};
    Parameter depth{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    /// Counterbore: diameter and depth of the wider part.
    Parameter headDiameter{.name = "", .expression = "11", .quantity = Quantity::Length, .value = 11.0, .error = {}};
    Parameter headDepth{.name = "", .expression = "6.4", .quantity = Quantity::Length, .value = 6.4, .error = {}};
    /// Countersink: diameter at the surface; the cone is 90°.
    Parameter sinkDiameter{.name = "", .expression = "13", .quantity = Quantity::Length, .value = 13.0, .error = {}};
    /// Резьбовое отверстие (0.2.4.1): drilled to the thread's minor diameter
    /// (as for a tap; `diameter` is not used then) with an ISO metric thread
    /// cut in its wall, from the face down to `threadDepth` or the whole hole.
    bool      threaded = false;
    double    threadSize = 6;      ///< nominal diameter, mm (doc::StandardSizes)
    Parameter threadPitch{.name = "", .expression = "1", .quantity = Quantity::Length, .value = 1.0, .error = {}};
    bool      threadFull = true;   ///< the whole depth of the hole
    Parameter threadDepth{.name = "", .expression = "8", .quantity = Quantity::Length, .value = 8.0, .error = {}};
    bool      threadLeft = false;  ///< left-handed

    /// The diameter actually drilled: the minor diameter of a threaded hole.
    double DrillDiameter() const;

    std::string_view Type() const override { return kType; }
    /// Removes centre `index` and the axis that held it.
    void RemoveCentre(std::size_t index);
    /// The axis holding centre `index`, if any.
    const HoleAxis* AxisOf(std::size_t index) const;
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
