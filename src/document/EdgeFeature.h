#pragma once
// Operations applied to edges of the existing body: fillet and chamfer.
// They act on the most recent body before them (as joins and cuts do).

#include "document/EdgeReference.h"
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

/// Bevels edges with an equal distance on both faces.
class ChamferFeature final : public FeatureBase<ChamferFeature, EdgeFeature> {
public:
    static constexpr std::string_view kType = "chamfer";

    Parameter distance{.name = "", .expression = "1", .quantity = Quantity::Length, .value = 1.0, .error = {}};

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override { out.push_back(&distance); }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
