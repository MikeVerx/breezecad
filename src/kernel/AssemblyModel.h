#pragma once
// Geometry of an assembly: loads and rebuilds the parts its components
// refer to, places their bodies and shows them as one kernel::Model whose
// bodies are the components (body id = component id). The viewport, picking
// and interference checks then work on an assembly as on a part.
// Free of OCCT types, like Model.h.

#include "assembly/Assembly.h"
#include "core/Expected.h"
#include "document/Document.h"
#include "document/SketchFeature.h"
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
    Any,      ///< a vertex or an edge within the tolerance (Inventor's mate), else the face
    Line,     ///< a straight edge within the tolerance, else the axis of the round face
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

    /// A part being edited in this program (in place, or just created in the
    /// assembly): shown from `doc` and `model` in memory instead of its file,
    /// until ForgetPartInMemory. The caller keeps both alive and rebuilt;
    /// Update notices a new model revision. `file`: the part's file (also
    /// for one not written yet), as components refer to it.
    void UsePartInMemory(const std::filesystem::path& file, const doc::Document* doc, const Model* model);
    /// Back to the file (read again at the next Update: it may have been saved).
    void ForgetPartInMemory(const std::filesystem::path& file);
    /// Whether the part of `file` comes from memory.
    bool IsPartInMemory(const std::filesystem::path& file) const;

    /// Files are looked at for changes at most this often (seconds); a new
    /// component's file at once. 0 (the default): at every Update.
    void SetDiskCheckInterval(double seconds);

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
    /// `tolerance` (mm, in the assembly): how near a vertex or an edge the
    /// point must be for Any and Line to take it rather than the face.
    std::optional<assembly::GeometryRef> GeometryAt(const assembly::Assembly& assembly, ObjectId component,
                                                    Vec3 worldPoint, AssemblyPick want, double tolerance = 0.0) const;
    /// A named element of a component's part (an origin plane/axis/point, a
    /// work plane, axis or point) or of the assembly origin, in the part's
    /// coordinates; nullopt when there is no such element (or it is not built).
    std::optional<assembly::GeometryRef> ElementGeometry(ObjectId component, ObjectId element) const;
    /// The named elements a constraint can use: the part's origin first,
    /// then its work planes, axes and points, with their tree names.
    struct NamedElement {
        ObjectId    id;
        std::string name;
        assembly::GeometryKind kind;
    };
    std::vector<NamedElement> ElementsOf(ObjectId component) const;
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

    /// Every other visible component, placed relative to `edited` (in its
    /// part's coordinates): what surrounds a part edited in place. Body ids
    /// are component ids. Shapes are shared with the parts (only moved).
    Model ContextOf(const assembly::Assembly& assembly, ObjectId edited) const;
    /// Bumped whenever any part's geometry changes (a reload, a part in memory rebuilt).
    std::uint64_t PartsGeneration() const;
    /// Changes when the geometry of `component`'s part changes; 0 without a part.
    std::uint64_t PartGenerationOf(ObjectId component) const;

    // --- projections across parts (AssemblyContext.cpp) ---------------------
    /// An edge picked on the surroundings of the part of `target` (a body of
    /// ContextOf: in that part's coordinates) described on the part of
    /// `source` in its own coordinates, to be found again there; nullopt
    /// when it is not there.
    std::optional<doc::EdgeReference> SourceEdge(const assembly::Assembly& assembly, ObjectId target, ObjectId source,
                                                 const doc::EdgeReference& inTarget) const;
    /// The edge of `component`'s part nearest `worldPoint` (in the assembly)
    /// within `tolerance` mm, described in the part's own coordinates (what a
    /// projection from another part keeps); nullopt when none is that near.
    std::optional<doc::EdgeReference> EdgeNear(const assembly::Assembly& assembly, ObjectId component, Vec3 worldPoint,
                                               double tolerance) const;
    /// Where the source edge of `link` projects onto `plane` of the part of
    /// `target` with the components where they stand now. `now`: the edge as
    /// it is now, described again (so it is followed from there next time).
    Expected<ProjectedCurve> ProjectSourceEdge(const assembly::Assembly& assembly, ObjectId target,
                                               const doc::ContextProjection& link, const Frame& plane,
                                               doc::EdgeReference* now = nullptr) const;

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
