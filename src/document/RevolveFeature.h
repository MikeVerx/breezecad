#pragma once

#include "document/SketchSolidFeature.h"

namespace bz::doc {

/// What the regions turn around: a line of the same sketch, normally an
/// осевая the user drew. SketchX / SketchY are only read from older files.
enum class RevolveAxisKind : unsigned char { SketchLine, SketchX, SketchY };

struct RevolveAxis {
    RevolveAxisKind kind = RevolveAxisKind::SketchLine;
    ObjectId        line;   ///< SketchLine only
};

/// How much of a turn.
enum class RevolveExtent : unsigned char {
    Full,        ///< 360°
    Angle,       ///< `angle` from the sketch plane, `reversed` turns the other way
    Symmetric,   ///< `angle` in total, half to each side of the sketch plane
};

/// Turns sketch regions around an axis lying in the sketch plane.
class RevolveFeature final : public FeatureBase<RevolveFeature, SketchSolidFeature> {
public:
    static constexpr std::string_view kType = "revolve";

    RevolveAxis   axis;
    RevolveExtent extent   = RevolveExtent::Full;
    bool          reversed = false;
    Parameter     angle{.name = "", .expression = "90", .quantity = Quantity::Angle, .value = kPi / 2, .error = {}};

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
