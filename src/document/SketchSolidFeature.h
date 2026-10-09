#pragma once
// Common part of operations that turn closed sketch regions into a solid:
// extrusion, revolution (later sweep, loft). They share the sketch, the
// chosen regions and how the result combines with the existing body.

#include "core/Math.h"
#include "document/Feature.h"

#include <vector>

namespace bz::doc {

/// How a new solid combines with the existing body.
enum class BooleanOp : unsigned char { NewBody, Join, Cut, Intersect };

/// Which closed regions of the sketch are used.
struct ProfileSelection {
    /// Automatic: every outer region, holes stay holes (a plate with a
    /// circle inside becomes a plate with a hole).
    bool automatic = true;
    /// Manual: one point inside each chosen region, in sketch coordinates.
    /// Points survive dimension changes better than region indices.
    std::vector<Vec2> regions;
};

class SketchSolidFeature : public Feature {
public:
    ObjectId         sketch;
    ProfileSelection profiles;
    BooleanOp        operation = BooleanOp::NewBody;

    std::vector<ObjectId> Dependencies() const override { return {sketch}; }

protected:
    SketchSolidFeature() = default;
    SketchSolidFeature(const SketchSolidFeature&) = default;
    SketchSolidFeature& operator=(const SketchSolidFeature&) = default;

    /// The shared fields, written and read by the derived SaveFields/LoadFields.
    void SaveSolidFields(nlohmann::json& out) const;
    /// `what` names the operation in error messages ("Выдавливание").
    Status LoadSolidFields(const nlohmann::json& in, const std::string& what);
};

/// Names of the operations in files ("join").
std::string_view BooleanOpId(BooleanOp op);
bool             BooleanOpFromId(std::string_view id, BooleanOp& out);

} // namespace bz::doc
