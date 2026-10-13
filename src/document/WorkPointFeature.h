#pragma once
// A work point ("Точка"): a point in the model, for planes through three
// points, axes through two and (later) assembly constraints. Placed one of
// these ways:
//   Vertex        at a vertex of the body (found again by an edge ending there);
//   Center        the centre of a round edge (circle, arc, ellipse) or of a sphere;
//   Midpoint      the middle of an edge;
//   Intersection  where a straight edge or a work axis crosses a plane;
//   Coordinates   at typed X, Y, Z.

#include "document/EdgeReference.h"
#include "document/FaceReference.h"
#include "document/Feature.h"

#include <optional>

namespace bz::doc {

enum class WorkPointMethod : unsigned char { Vertex, Center, Midpoint, Intersection, Coordinates };

class WorkPointFeature final : public FeatureBase<WorkPointFeature> {
public:
    static constexpr std::string_view kType = "workpoint";

    WorkPointMethod method = WorkPointMethod::Vertex;
    /// Vertex: where it was picked (updated to where the vertex is now).
    Vec3 vertex;
    /// Vertex: an edge ending there; Center/Midpoint/Intersection: the edge.
    std::optional<EdgeReference> edge;
    /// Center: a sphere (instead of `edge`).
    std::optional<FaceReference> sphere;
    /// Intersection: a work axis (instead of `edge`) and the plane.
    ObjectId axis;
    ObjectId plane;
    Parameter x{.name = "", .expression = "0", .quantity = Quantity::Length, .value = 0.0, .error = {}};
    Parameter y{.name = "", .expression = "0", .quantity = Quantity::Length, .value = 0.0, .error = {}};
    Parameter z{.name = "", .expression = "0", .quantity = Quantity::Length, .value = 0.0, .error = {}};

    /// Where the point is after the last rebuild (runtime, never saved).
    std::optional<Vec3> position;

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void CollectParameters(std::vector<Parameter*>& out) override {
        out.push_back(&x);
        out.push_back(&y);
        out.push_back(&z);
    }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
