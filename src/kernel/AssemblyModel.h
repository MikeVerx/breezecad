#pragma once
// Geometry of an assembly: loads and rebuilds the parts its components
// refer to, places their bodies and shows them as one kernel::Model whose
// bodies are the components (body id = component id). The viewport, picking
// and interference checks then work on an assembly as on a part.
// Free of OCCT types, like Model.h.

#include "assembly/Assembly.h"
#include "core/Expected.h"
#include "document/Document.h"
#include "kernel/Model.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bz::kernel {

/// What a click on a component should give a constraint.
enum class AssemblyPick : unsigned char {
    Face,     ///< the face under the cursor: plane, cylinder, sphere (cone/torus: their axis)
    Circle,   ///< the circular edge nearest the cursor (Вставка)
    Axis,     ///< the axis of the round face under the cursor (Совпадение осей)
};

/// What is around a hole: the stack of components its axis goes through.
struct HoleStack {
    double                thickness = 0;   ///< mm, from the picked rim to where the material ends
    ObjectId              last;            ///< the component the stack ends in
    assembly::GeometryRef farRim;          ///< the hole's rim on the far side (Circle), when found
};

/// A pair of components that share material.
struct ComponentClash {
    ObjectId first, second;   ///< component ids
    double   volume = 0.0;    ///< mm^3
    Vec3     centre;          ///< in the assembly
};

class AssemblyModel {
public:
    AssemblyModel();
    ~AssemblyModel();
    AssemblyModel(AssemblyModel&&) noexcept;
    AssemblyModel& operator=(AssemblyModel&&) noexcept;

    /// Loads parts not loaded yet (or changed on disk since) and rebuilds
    /// the shown model when the assembly changed. `assemblyFile` resolves
    /// relative paths (empty for an unsaved assembly). Problems (a missing
    /// part file...) are reported once per part, in `problems`.
    void Update(assembly::Assembly& assembly, const std::filesystem::path& assemblyFile,
                std::vector<std::string>* problems = nullptr);
    /// Forgets loaded parts so the next Update reads them again.
    void ReloadParts();

    /// The components as bodies of one model (for the viewport).
    const Model& Shown() const;
    /// Builds the shown model into `target` (a session's model) instead of
    /// its own; `target` must outlive this object.
    void ShowIn(Model& target);

    /// The part a component shows (loaded and rebuilt), or nullptr when its
    /// file could not be read.
    const doc::Document* PartOf(ObjectId component) const;
    /// Its bodies before placing (in the part's own coordinates), or nullptr.
    const Model* PartModelOf(ObjectId component) const;
    /// Copies of its part's bodies placed at `at` (the component's placement):
    /// a separate model in assembly coordinates, nullopt without geometry.
    std::optional<Model> PlacedModelOf(ObjectId component, const assembly::Placement& at) const;
    /// Why a component has no geometry ("Файл не найден: …"), or empty.
    std::string ProblemOf(ObjectId component) const;

    /// Geometry for a constraint at `worldPoint` on component `component`,
    /// in the part's coordinates. nullopt when nothing fitting is there.
    std::optional<assembly::GeometryRef> GeometryAt(const assembly::Assembly& assembly, ObjectId component,
                                                    Vec3 worldPoint, AssemblyPick want) const;
    /// Finds a constraint's geometry again after its part changed (by the
    /// point it was picked at). False when it is gone.
    bool Refresh(const assembly::Assembly& assembly, assembly::GeometryRef& ref) const;

    /// The stack of material under a hole's rim `rim` (a Circle picked with
    /// GeometryAt): along the axis, into the material, across touching
    /// components. Measured beside the hole, just outside its radius.
    /// Nullopt when there is no material there.
    std::optional<HoleStack> StackAt(const assembly::Assembly& assembly, const assembly::GeometryRef& rim) const;

    /// Holes of `rim`'s component like it: rims of the same radius on the
    /// same plane, facing the same way (a pattern of holes, a bolt circle...),
    /// in the part's coordinates; `rim` itself first, then by distance.
    std::vector<assembly::GeometryRef> SameHoles(const assembly::Assembly& assembly,
                                                 const assembly::GeometryRef& rim) const;

    /// Box of a component in the assembly; nullopt without geometry.
    std::optional<Bounds> ComponentBounds(ObjectId component) const;

    /// Pairs of components that overlap (touching does not count).
    std::vector<ComponentClash> Clashes() const;

    struct Impl;
    const Impl& GetImpl() const { return *myImpl; }

private:
    std::unique_ptr<Impl> myImpl;
};

/// Loads a part file and rebuilds it (what AssemblyModel does for each part).
struct LoadedPart {
    doc::Document doc;
    Model         model;
};
Expected<LoadedPart> LoadPart(const std::filesystem::path& file);

} // namespace bz::kernel
