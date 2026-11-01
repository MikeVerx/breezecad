#pragma once

#include "document/FaceReference.h"
#include "document/SketchSolidFeature.h"

#include <optional>
#include <string>

namespace bz::doc {

/// How far an extrusion goes.
enum class ExtentType : unsigned char {
    Distance,     ///< `distance` along the direction
    Symmetric,    ///< `distance` in total, half to each side of the sketch
    ThroughAll,   ///< through the whole body (cuts)
    ToPlane,      ///< up to a plane: an origin or work plane (`toPlane`) or a flat face (`toFace`)
    Asymmetric,   ///< `distance` along the direction and `distance2` the other way (Inventor's "Asymmetric")
};

/// "До грани" of another part, chosen while this part is edited in an
/// assembly: the plane is kept here (the part opened on its own extrudes to
/// it as it last was); in that assembly it follows the face when the other
/// part changes or moves (kernel/ContextProjection.h).
struct ContextPlane {
    std::string   assembly;          ///< the assembly it came from: its file name without extension
    ObjectId      component;         ///< the other part's component in that assembly
    FaceReference face;              ///< that part's flat face, in its own coordinates
    Frame         plane;             ///< where the face lies, in this part's coordinates
    std::string   componentName;     ///< for the panel ("грань «Корпус:1»")
    bool          adaptive = true;   ///< follows the face; off: stays where it is
};

/// Sweeps sketch regions along the sketch normal.
class ExtrudeFeature final : public FeatureBase<ExtrudeFeature, SketchSolidFeature> {
public:
    static constexpr std::string_view kType = "extrude";

    ExtentType extent   = ExtentType::Distance;
    bool       reversed = false;
    Parameter  distance{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    Parameter  taper{.name = "", .expression = "0", .quantity = Quantity::Angle, .value = 0.0, .error = {}};
    /// Asymmetric: the distance on the other side of the sketch.
    Parameter  distance2{.name = "", .expression = "5", .quantity = Quantity::Length, .value = 5.0, .error = {}};
    ObjectId      toPlane;   ///< ToPlane: an origin plane or a work plane
    FaceReference toFace;    ///< ToPlane: a flat face of the body (when `toPlane` is not set)
    std::optional<ContextPlane> toContext;   ///< ToPlane: a face of another part of an assembly (first when set)

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override {
        std::vector<ObjectId> out{sketch};
        if (extent == ExtentType::ToPlane && toPlane) out.push_back(toPlane);
        return out;
    }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

std::string_view ExtentTypeId(ExtentType extent);

} // namespace bz::doc
