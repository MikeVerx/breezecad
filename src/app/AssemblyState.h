#pragma once
// What an open assembly adds to its Session: the assembly itself, the
// geometry of its parts, undo history and what the assembly panels show.

#include "assembly/Assembly.h"
#include "kernel/AssemblyModel.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace bz::app {

class AssemblyState {
public:
    assembly::Assembly    assembly;
    kernel::AssemblyModel geometry;

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
    } drag;

    /// A constraint being made: its type, options and what is picked so far.
    struct NewConstraint {
        bool                    active = false;
        assembly::Constraint    constraint;     ///< type, value, flipped; first/second filled by picks
        int                     picked = 0;     ///< 0, 1 (first chosen)
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

    /// Freedom left to each component after the last solve (tree badges).
    std::unordered_map<ObjectId, assembly::Freedom> freedom;
    /// Problems found by the last Update (missing part files...), to show once.
    std::vector<std::string> problems;

    /// Remembers the assembly as it is, before a change (one undo step).
    void Checkpoint(std::string label);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !myUndo.empty(); }
    bool CanRedo() const { return !myRedo.empty(); }
    std::string UndoLabel() const { return myUndo.empty() ? std::string{} : myUndo.back().label; }
    std::string RedoLabel() const { return myRedo.empty() ? std::string{} : myRedo.back().label; }

    /// Saved-state marker, like doc::History.
    void MarkSaved() { mySaved = assembly.Revision(); myUnsaved = false; }
    void MarkUnsaved() { myUnsaved = true; }
    bool IsModified() const { return myUnsaved || assembly.Revision() != mySaved; }

    /// Loads changed parts, solves constraints when the assembly changed and
    /// refreshes what is shown. True when the shown geometry changed.
    bool Update(const std::filesystem::path& file);

private:
    struct Step {
        std::string json, label;
    };
    std::vector<Step> myUndo, myRedo;
    std::uint64_t     mySaved = 0;
    bool              myUnsaved = false;
    std::uint64_t     mySolvedRevision = ~std::uint64_t{0};

    void Restore(const std::string& json);
};

} // namespace bz::app
