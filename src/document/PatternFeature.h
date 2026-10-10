#pragma once
// Operations that repeat other operations: a rectangular or circular
// pattern, and a mirror. They copy what the source operations added or cut
// (an extrusion, a revolution, holes) and apply the copies to the most
// recent body, so a patterned hole is a hole and a mirrored boss is a boss.

#include "core/Expected.h"
#include "core/Math.h"
#include "document/Feature.h"

#include <vector>

namespace bz::doc {

/// Model axes a pattern runs along or turns around (through the origin).
enum class ModelAxis : unsigned char { X, Y, Z };
Vec3 AxisDirection(ModelAxis axis);

/// Common part: which operations are repeated.
class CopyFeature : public Feature {
public:
    std::vector<ObjectId> sources;   ///< operations before this one, in document order

    std::vector<ObjectId> Dependencies() const override { return sources; }

protected:
    CopyFeature() = default;
    CopyFeature(const CopyFeature&) = default;
    CopyFeature& operator=(const CopyFeature&) = default;

    void SaveSources(nlohmann::json& out) const;
    Status LoadSources(const nlohmann::json& in, const std::string& what);
};

enum class PatternKind : unsigned char { Rectangular, Circular };

class PatternFeature final : public FeatureBase<PatternFeature, CopyFeature> {
public:
    static constexpr std::string_view kType = "pattern";

    PatternKind kind = PatternKind::Rectangular;

    // Rectangular: copies along one or two model axes.
    ModelAxis direction1 = ModelAxis::X;
    bool      reversed1 = false;
    Parameter count1{.name = "", .expression = "3", .quantity = Quantity::Scalar, .value = 3.0, .error = {}};
    Parameter spacing1{.name = "", .expression = "20", .quantity = Quantity::Length, .value = 20.0, .error = {}};
    bool      useDirection2 = false;
    ModelAxis direction2 = ModelAxis::Y;
    bool      reversed2 = false;
    Parameter count2{.name = "", .expression = "2", .quantity = Quantity::Scalar, .value = 2.0, .error = {}};
    Parameter spacing2{.name = "", .expression = "20", .quantity = Quantity::Length, .value = 20.0, .error = {}};

    // Circular: copies around a model axis, spread evenly over `angle`
    // (a full turn closes the circle, so its last copy is not doubled).
    ModelAxis axis = ModelAxis::Z;
    ObjectId  workAxis;   ///< a work axis to turn around instead of `axis` (set: wins)
    Parameter circularCount{.name = "", .expression = "6", .quantity = Quantity::Scalar, .value = 6.0, .error = {}};
    Parameter angle{.name = "", .expression = "360", .quantity = Quantity::Angle, .value = 2 * kPi, .error = {}};

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override {
        std::vector<ObjectId> out = sources;
        if (workAxis) out.push_back(workAxis);
        return out;
    }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;

    /// Placements of the copies (the original excluded), or an error for
    /// bad counts. Each is a rotation about `axis` then a move; see Copy.
    struct Copy {
        Vec3   offset;          ///< rectangular move
        double turn = 0.0;      ///< circular rotation, radians
    };
    Expected<std::vector<Copy>> Copies() const;
};

class MirrorFeature final : public FeatureBase<MirrorFeature, CopyFeature> {
public:
    static constexpr std::string_view kType = "mirror";

    ObjectId plane;   ///< origin plane or work plane
    bool     wholeBody = false;   ///< mirror the whole most recent body (the sources are not used)

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

} // namespace bz::doc
