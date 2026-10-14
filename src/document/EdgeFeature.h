#pragma once
// Operations applied to edges of the existing body: fillet and chamfer.
// They act on the most recent body before them (as joins and cuts do).

#include "document/EdgeReference.h"
#include "core/Math.h"
#include "document/Feature.h"

#include <vector>

namespace bz::doc {

class EdgeFeature : public Feature {
public:
    std::vector<EdgeReference> edges;

protected:
    EdgeFeature() = default;
    EdgeFeature(const EdgeFeature&) = default;
    EdgeFeature& operator=(const EdgeFeature&) = default;

    void SaveEdges(nlohmann::json& out) const;
    /// `what` names the operation in error messages.
    Status LoadEdges(const nlohmann::json& in, const std::string& what);
};

/// Rounds edges with a constant radius.
class FilletFeature final : public FeatureBase<FilletFeature, EdgeFeature> {
public:
    static constexpr std::string_view kType = "fillet";

    Parameter radius{.name = "", .expression = "2", .quantity = Quantity::Length, .value = 2.0, .error = {}};

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override { out.push_back(&radius); }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// How a chamfer is sized.
enum class ChamferMode : unsigned char {
    EqualDistance,   ///< `distance` on both faces (45°)
    DistanceAngle,   ///< `distance` on the reference face, the bevel at `angle` to that face
    TwoDistances,    ///< `distance` on the reference face, `distance2` on the other
};

/// Bevels edges. Each edge has two faces; the reference face of an edge is
/// found by its geometry (see kernel::occt::MakeChamfer). `flipped` takes the
/// other one for every edge; an edge listed in `flippedEdges` is turned once
/// more (the per-edge "flip" of Inventor).
class ChamferFeature final : public FeatureBase<ChamferFeature, EdgeFeature> {
public:
    static constexpr std::string_view kType = "chamfer";

    ChamferMode mode = ChamferMode::EqualDistance;
    Parameter distance{.name = "", .expression = "1", .quantity = Quantity::Length, .value = 1.0, .error = {}};
    Parameter distance2{.name = "", .expression = "1", .quantity = Quantity::Length, .value = 1.0, .error = {}};
    Parameter angle{.name = "", .expression = "45", .quantity = Quantity::Angle, .value = kPi / 4, .error = {}};
    bool      flipped = false;
    std::vector<EdgeReference> flippedEdges;   ///< edges (of `edges`) with their own side turned

    /// Edge `index` of `edges` uses the other face as its reference.
    bool FlippedAt(std::size_t index) const;
    /// Turns the side of edge `index` of `edges`.
    void FlipAt(std::size_t index);

    std::string_view Type() const override { return kType; }
    /// Always all three, in this order, whatever the mode (panels copy them by position).
    void CollectParameters(std::vector<Parameter*>& out) override {
        out.push_back(&distance);
        out.push_back(&distance2);
        out.push_back(&angle);
    }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
