#pragma once
// A modelled thread ("Резьба"): an ISO metric 60° thread on a cylindrical
// face of the body. On a shaft the groove goes in from its surface
// (external thread); in a hole it goes out into the wall (internal thread,
// the hole being the minor diameter, as drilled for a tap).

#include "document/Feature.h"
#include "document/WorkPlaneFeature.h"

#include <optional>

namespace bz::doc {

class ThreadFeature final : public FeatureBase<ThreadFeature> {
public:
    static constexpr std::string_view kType = "thread";

    std::optional<CylinderTouch> face;        ///< the shaft or the hole (`radial` unused)
    Parameter pitch{.name = "", .expression = "1.25", .quantity = Quantity::Length, .value = 1.25, .error = {}};
    bool      fullLength = true;
    Parameter length{.name = "", .expression = "10", .quantity = Quantity::Length, .value = 10.0, .error = {}};
    bool      fromOtherEnd = false;   ///< start at the other end of the face
    bool      leftHanded = false;
    /// Standard size chosen by hand (nominal diameter, mm); 0 = from the
    /// face. A shaft or hole of another diameter is cut to this size.
    double    size = 0;
    /// Drawn on the face, not cut: the model stays light. The real thread
    /// is cut in exported files.
    bool      cosmetic = false;

    /// Runtime (set by the rebuild, not saved): what the face turned out to be.
    struct Site {
        bool   external = true;   ///< a shaft, not a hole
        double nominal = 0;       ///< nominal diameter, mm
        double length = 0;        ///< face length along the axis, mm
    };
    std::optional<Site> site;

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override {
        out.push_back(&pitch);
        out.push_back(&length);
    }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// ISO metric coarse pitch for a nominal diameter (the nearest standard size).
double CoarsePitch(double nominalDiameter);

/// The ISO 261 coarse series, smallest first: nominal diameters, mm.
std::vector<double> StandardSizes();

/// The standard size `diameter` is within 5% of, or `diameter` itself.
double StandardSize(double diameter);

} // namespace bz::doc
