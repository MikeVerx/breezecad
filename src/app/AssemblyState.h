#pragma once
// What an open assembly adds to its Session: the assembly itself, the
// geometry of its parts, undo history and what the assembly panels show.

#include "assembly/Assembly.h"
#include "kernel/AssemblyModel.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bz::app {

class Session;

class AssemblyState {
public:
    AssemblyState();
    ~AssemblyState();
    AssemblyState(const AssemblyState&) = delete;
    AssemblyState& operator=(const AssemblyState&) = delete;

    assembly::Assembly    assembly;
    kernel::AssemblyModel geometry;

    /// A part edited in this assembly (in place, or created here): its own
    /// document with its own undo history, kept in memory until the assembly
    /// is saved (as in Inventor), then written into its file beside the
    /// assembly. The assembly shows it from memory meanwhile.
    struct PartEdit {
        std::filesystem::path    file;      ///< absolute; where it is written (a new part: not there yet)
        std::unique_ptr<Session> session;
        bool                     created = false;   ///< made in this assembly, never written yet
        /// No component used it at the last save (its creation undone): kept
        /// for a redo, not a change waiting to be saved.
        bool                     unused = false;
    };
    std::vector<PartEdit> parts;
    /// The component whose part is edited in place right now; none: the
    /// assembly itself is being edited.
    ObjectId inPlace;

    /// The part session edited in place, or nullptr.
    Session* InPlaceSession();
    const Session* InPlaceSession() const;
    /// The edit of the part in `file`, or nullptr.
    PartEdit* EditOf(const std::filesystem::path& file);
    /// Some part edited here has changes not written yet.
    bool PartsModified() const;

    ObjectId selected;   ///< a component or a constraint (tree and view)

    /// What a drag on a component in the view does.
    enum class Tool : unsigned char { Move, Rotate };
    Tool tool = Tool::Move;
    /// A component being dragged in the view.
    struct Drag {
        bool                armed = false;    ///< pressed on a component, not moved yet
        bool                moving = false;
        ObjectId            component;
        Vec2                pressAt;          ///< pixels
        Vec3                grab;             ///< the point pressed, in the assembly
        assembly::Placement start;            ///< where the component was at the press
        Vec3                pivot;            ///< rotating: centre of its box
        Vec3                local;            ///< the point pressed, in the component's coordinates
        std::optional<Vec3> target;           ///< moving: where the cursor pulls that point
        Vec2                lastPixel{-1e9, -1e9};   ///< the cursor the last move was made for
    } drag;

    /// A constraint being made: its type, options and what is picked so far.
    struct NewConstraint {
        bool                    active = false;
        assembly::Constraint    constraint;     ///< type, value, flipped; first/second (third) filled by picks
        int                     picked = 0;     ///< 0, 1 (first chosen), 2 (symmetry: both chosen, the plane next)
        /// "Переопределить": the picks replace the geometry (and type) of
        /// this constraint instead of making a new one.
        ObjectId                redefine;
        /// All picked: the constraint is already in the assembly (the parts
        /// moved, to be judged and tuned) until Применить / Готово keep it
        /// or Отмена / Esc take it back.
        ObjectId                preview;
    } creating;

    /// AutoDrop: a screw from the library waits for a click on a hole's rim.
    struct Drop {
        bool        active = false;
        std::string screw, washer, nut;   ///< standard ids; washer/nut empty: none
    } dropping;

    /// After AutoDrop: "the same into all such holes?" for the screw just put.
    struct Offer {
        ObjectId component;   ///< the screw; none: no offer
        int      holes = 0;   ///< such holes in all, with its own
    } offer;
    /// A circular pattern waits for a click on a round face for its axis.
    ObjectId pickingAxis;

    /// "Создать": a new part waits for the face or plane its sketch starts on.
    struct NewPart {
        bool        active = false;
        std::string name;   ///< as typed in the panel
    } newPart;

    /// Freedom left to each component after the last solve (tree badges).
    std::unordered_map<ObjectId, assembly::Freedom> freedom;
    /// Problems found by the last Update (missing part files...), to show once.
    std::vector<std::string> problems;

    /// What is wrong with the assembly now, for the two counters in the
    /// header: red for part files (missing or unreadable), yellow for the
    /// assembly itself (constraints that do not hold or repeat others).
    struct Issue {
        enum class Kind : unsigned char { MissingFile, BrokenFile, FailedConstraint, RedundantConstraint };
        Kind        kind;
        ObjectId    id;        ///< the component or the constraint
        std::string name;      ///< as in the tree
        std::string detail;    ///< why (Russian, translated when shown)
        bool        Red() const { return kind == Kind::MissingFile || kind == Kind::BrokenFile; }
    };
    std::vector<Issue> Issues() const;

    /// Remembers the assembly as it is, before a change (one undo step).
    void Checkpoint(std::string label);
    bool Undo();
    /// Undo that leaves nothing to redo: a trial (a constraint previewed) taken back.
    bool Discard();
    bool Redo();
    bool CanUndo() const { return !myUndo.empty(); }
    bool CanRedo() const { return !myRedo.empty(); }
    std::string UndoLabel() const { return myUndo.empty() ? std::string{} : myUndo.back().label; }
    std::string RedoLabel() const { return myRedo.empty() ? std::string{} : myRedo.back().label; }

    /// Saved-state marker, like doc::History.
    void MarkSaved() { mySaved = assembly.Revision(); mySavedLook = assembly.LookRevision(); myUnsaved = false; }
    void MarkUnsaved() { myUnsaved = true; }
    bool IsModified() const { return myUnsaved || assembly.Revision() != mySaved || assembly.LookRevision() != mySavedLook; }

    /// What the view shows: each component in its part's own appearances
    /// (bodies and faces, placed), with this assembly's overrides on top
    /// (Assembly::look). Made again when any of them changes.
    const doc::Look& ShownLook();
    /// Bumped whenever ShownLook gives something new.
    std::uint64_t ShownLookRevision() const { return myShownLookRevision; }

    /// Loads changed parts, solves constraints when the assembly changed and
    /// refreshes what is shown; parts with projections from this assembly
    /// (named `contextName`, see ContextName) follow their sources. True when
    /// the shown geometry changed.
    bool Update(const std::filesystem::path& file, std::string_view contextName = {});

    /// The assembly as it stands, to come back to when a tool fails half-way
    /// (app/Recovery.h): its contents, undo/redo, saved state, the part edited
    /// in place and how many parts are edited in it.
    struct Mark {
        std::string       json;
        std::uint64_t     revision = 0, lookRevision = 0;
        std::size_t       undo = 0;
        std::vector<std::string> redoJson, redoLabels;
        std::uint64_t     saved = 0, savedLook = 0;
        bool              unsaved = false;
        ObjectId          inPlace, selected;
        std::size_t       parts = 0;
    };
    /// Cheap when `previous` is still current (only the contents are compared by revision).
    Mark GetMark(const Mark* previous = nullptr) const;
    /// Nothing changed since `mark` was taken (cheap: revisions and counts).
    bool Matches(const Mark& mark) const {
        return mark.revision == assembly.Revision() && mark.lookRevision == assembly.LookRevision() && mark.undo == myUndo.size() &&
               mark.redoJson.size() == myRedo.size() && mark.saved == mySaved && mark.savedLook == mySavedLook &&
               mark.unsaved == myUnsaved && mark.inPlace == inPlace && mark.selected == selected && mark.parts == parts.size();
    }
    /// Back to `mark`: contents, history and saved state; parts taken into
    /// memory since are let go; whatever was being picked or dragged is dropped.
    void RollBack(const Mark& mark);

    /// "Снять адаптивность" (Inventor's adaptivity unticked): the part of
    /// `component` stops following other parts of this assembly (named `name`,
    /// see Session::ContextName) — its projections and extrusions up to their
    /// faces stay where they are now. Undone with the assembly's undo; the part
    /// is written with the assembly. False when it had nothing to turn off or
    /// could not now (`why` says what to do).
    bool DropAdaptivity(const std::filesystem::path& file, ObjectId component, std::string_view name, std::string* why = nullptr);

    /// Adaptivity starts afresh (the assembly's name for its parts changed).
    void ForgetAdaptState() {
        myAdaptKeys.clear();
        myHasContext.clear();
    }

    /// The other components, placed as they stand around the part edited in
    /// place, in that part's coordinates: the context shown faded while it is
    /// edited (and what "Проецировать" can take edges from). Each body's id
    /// is its component's. Made again only when something moved or changed.
    const kernel::Model* InPlaceContext();

private:
    struct Step {
        std::string json, label;
        /// Parts that followed their sources because of this step, as they
        /// were before: undo puts them back too (the sketches with them).
        std::vector<std::pair<std::filesystem::path, std::string>> partsBefore;
        std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
    };
    std::vector<Step> myUndo, myRedo;
    std::uint64_t     mySaved = 0;
    std::uint64_t     mySavedLook = 0;
    doc::Look         myShownLook;
    std::uint64_t     myShownLookRevision = 0;
    std::array<std::uint64_t, 3> myShownLookKey{~std::uint64_t{0}, 0, 0};
    bool              myUnsaved = false;
    std::uint64_t     mySolvedRevision = ~std::uint64_t{0};
    std::uint64_t     myAnalyzedRevision = ~std::uint64_t{0};
    std::unique_ptr<kernel::Model> myContext;
    /// Adaptivity: per part file, what its projections depended on when it
    /// last followed them, and whether it has any (by its geometry generation).
    std::unordered_map<std::string, std::uint64_t> myAdaptKeys;
    struct ContextInfo {
        std::uint64_t         generation = 0;
        bool                  has = false;
        std::vector<ObjectId> sources;   ///< the components it is drawn from
    };
    std::unordered_map<std::string, ContextInfo> myHasContext;
    /// Whether a part has projections from here, and from where: looked at once per version of it.
    const ContextInfo& ContextOf(const std::string& key, ObjectId component, const doc::Document& part, std::string_view name);
    /// Parts with projections follow their sources; true when one changed.
    bool Adapt(const std::filesystem::path& file, std::string_view name);
    /// Rounds of following since the last action: parts that follow each
    /// other through constraints would go on for ever (and drift away).
    int  myAdaptRounds = 0;
    bool myAdaptStopped = false;
    /// Marks the components whose parts follow others here (Component::follower).
    void MarkFollowers(const std::filesystem::path& file, std::string_view name);
    /// What a part was before it followed: kept with the step that made it follow.
    void RememberPartBefore(const std::filesystem::path& file, std::string json);
    std::array<std::uint64_t, 3>   myContextKey{~std::uint64_t{0}, 0, 0};

    void Restore(const std::string& json);
};

} // namespace bz::app
