#pragma once

#include "core/Id.h"
#include "core/Parameter.h"
#include "core/Units.h"
#include "document/Appearance.h"
#include "document/Feature.h"

#include <cstddef>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bz::doc {

/// Descriptive properties of a part (the "Свойства" tab).
struct PartInfo {
    std::string name;          ///< "Кронштейн"; empty = use the file name
    std::string designation;   ///< drawing number, e.g. "БРЗ.001.002"
    std::string material;      ///< free text for now, e.g. "Сталь 20"

    bool operator==(const PartInfo&) const = default;
};

/// A part: an ordered list of features (the recipe) plus user variables.
///
/// Editing goes through commands (Commands.h) so it can be undone; the
/// mutating methods here are the primitives those commands use.
class Document {
public:
    Document();
    Document(Document&&) noexcept = default;
    Document& operator=(Document&&) noexcept = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    /// A deep copy (for a rebuild on another thread).
    Document Clone() const;
    /// Takes the results of a rebuild done on Clone(), made at the current
    /// revision: evaluated values, feature statuses and what features
    /// resolved, and how far the rebuild got.
    void TakeRebuilt(const Document& rebuilt);

    PartInfo               info;
    UnitSystem             units;
    std::vector<Parameter> variables;   ///< the user variables table
    Look                   look;        ///< colours of bodies and faces (never rebuilt)

    // --- features -------------------------------------------------------
    const std::vector<std::unique_ptr<Feature>>& Features() const { return myFeatures; }
    std::size_t FeatureCount() const { return myFeatures.size(); }

    Feature*       Find(ObjectId id);
    const Feature* Find(ObjectId id) const;
    /// Index in the tree, or -1.
    int IndexOf(ObjectId id) const;

    template <class T>
    T* FindAs(ObjectId id) {
        return dynamic_cast<T*>(Find(id));
    }
    template <class T>
    const T* FindAs(ObjectId id) const {
        return dynamic_cast<const T*>(Find(id));
    }

    void Insert(std::unique_ptr<Feature> feature, std::size_t index);
    /// Removes a feature and returns it (for undo). Null if not found.
    std::unique_ptr<Feature> Take(ObjectId id);
    /// Replaces the feature with the same id. Returns the old one.
    std::unique_ptr<Feature> Replace(std::unique_ptr<Feature> feature);
    void Move(ObjectId id, std::size_t newIndex);

    /// End-of-part marker: features at index >= EndMarker() are rolled back
    /// (not computed), like the marker in Inventor's browser.
    std::size_t EndMarker() const { return myEndMarker; }
    void        SetEndMarker(std::size_t index);
    bool        IsActive(std::size_t index) const { return index < myEndMarker; }

    // --- naming ---------------------------------------------------------
    IdGenerator& Ids() { return myIds; }
    const IdGenerator& Ids() const { return myIds; }

    /// "Эскиз" -> "Эскиз1", "Эскиз2"... unique among features.
    std::string UniqueFeatureName(std::string_view base) const;
    /// Next free parameter name: "d1", "d2"...
    std::string NextParameterName() const;

    // --- parameters -----------------------------------------------------
    /// Variables first, then parameters of all features in tree order.
    std::vector<Parameter*> AllParameters();
    std::vector<const Parameter*> AllParameters() const;
    /// Evaluates every expression in dependency order. A parameter that
    /// fails keeps its previous value and gets an error message.
    /// Returns the number of parameters with errors.
    int EvaluateParameters();

    // --- change tracking ------------------------------------------------
    /// Bumped on every change; used by autosave and the viewport.
    std::uint64_t Revision() const { return myRevision; }
    /// The look changed (counts as a change for saving, needs no rebuild).
    void TouchLook() {
        ++myRevision;
        ++myLookRevision;
    }
    std::uint64_t LookRevision() const { return myLookRevision; }
    /// Marks features from `index` on as needing a rebuild.
    void MarkChanged(std::size_t fromIndex = 0);
    /// First feature index that needs a rebuild (FeatureCount() if none).
    std::size_t DirtyFrom() const { return myDirtyFrom; }
    void        ClearDirty() { myDirtyFrom = myFeatures.size(); }
    /// Features from `index` on still need a rebuild (a partial one stopped there).
    void        KeepDirtyFrom(std::size_t index) { myDirtyFrom = std::min(index, myFeatures.size()); }

private:
    std::vector<std::unique_ptr<Feature>> myFeatures;
    IdGenerator                           myIds;
    std::size_t                           myEndMarker = 0;
    std::uint64_t                         myRevision  = 0;
    std::uint64_t                         myLookRevision = 0;
    std::size_t                           myDirtyFrom = 0;
};

} // namespace bz::doc
