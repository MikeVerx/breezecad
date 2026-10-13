#pragma once

#include "core/Expected.h"
#include "core/Id.h"
#include "core/Parameter.h"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <type_traits>
#include <string>
#include <string_view>
#include <vector>

namespace bz::doc {

/// Result of the last recompute of a feature. Errors are part of the model
/// (shown in the tree), not exceptions: a broken feature does not stop the
/// document from opening.
enum class FeatureState : unsigned char {
    NotComputed,   ///< not recomputed yet, or after the end-of-part marker
    Ok,
    Warning,       ///< built, but something needs attention
    Error,         ///< could not be built; later features may still build
    Suppressed,    ///< switched off by the user
};

struct FeatureStatus {
    FeatureState state = FeatureState::NotComputed;
    std::string  message;   ///< shown to the user, in the UI language
};

/// One step of the part recipe: a sketch, an extrusion, a fillet...
///
/// A feature stores only its inputs (what the user chose and typed).
/// Geometry is computed from inputs by the kernel and never saved as the
/// source of truth. To add a new feature type: derive from Feature,
/// implement the virtuals and register a factory in FeatureRegistry.
class Feature {
public:
    virtual ~Feature() = default;

    ObjectId    id;
    std::string name;               ///< shown in the tree, e.g. "Эскиз1"
    bool        suppressed = false;
    /// Not drawn (a body, sketch, work plane, axis or point). Unlike
    /// `suppressed` it changes nothing else: geometry, mass and export stay.
    bool        hidden = false;

    /// Runtime state, never saved.
    FeatureStatus status;

    /// Stable type id used in files: "sketch", "extrude".
    virtual std::string_view Type() const = 0;

    virtual std::unique_ptr<Feature> Clone() const = 0;

    /// Takes what a rebuild computed on a copy of this feature (status,
    /// solved sketch, resolved references). The copy was made from this
    /// feature and nothing changed since, so the object stays the same one
    /// and pointers into it stay valid.
    virtual void TakeRebuilt(const Feature& rebuilt) { status = rebuilt.status; }

    /// Features this one reads from (e.g. the sketch of an extrusion).
    virtual std::vector<ObjectId> Dependencies() const { return {}; }

    /// Every editable number of this feature, for evaluation and for the
    /// variables table.
    virtual void CollectParameters(std::vector<Parameter*>& out) { (void)out; }

    /// Read-only view of CollectParameters().
    std::vector<const Parameter*> Parameters() const {
        std::vector<Parameter*> params;
        // CollectParameters only takes addresses; nothing is modified here.
        const_cast<Feature*>(this)->CollectParameters(params);
        return {params.begin(), params.end()};
    }

    /// Tells the generator about every id used inside the feature, so new
    /// ids stay unique after loading a file.
    virtual void ReserveIds(IdGenerator& ids) const { ids.Reserve(id); }

    /// Type-specific fields. Common fields (id, type, name, suppressed)
    /// are written by the file format code.
    virtual void SaveFields(nlohmann::json& out) const = 0;
    virtual Status LoadFields(const nlohmann::json& in) = 0;

protected:
    Feature() = default;
    Feature(const Feature&) = default;
    Feature& operator=(const Feature&) = default;
};

/// Clones a concrete feature type with its exact type preserved.
/// `Base` is Feature or an intermediate kind such as SketchSolidFeature.
template <class Derived, class Base = Feature>
class FeatureBase : public Base {
public:
    std::unique_ptr<Feature> Clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
    void TakeRebuilt(const Feature& rebuilt) override {
        if constexpr (std::is_copy_assignable_v<Derived>)
            static_cast<Derived&>(*this) = static_cast<const Derived&>(rebuilt);
        else
            Feature::TakeRebuilt(rebuilt);
    }
};

} // namespace bz::doc
