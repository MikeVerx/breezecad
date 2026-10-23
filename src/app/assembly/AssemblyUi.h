#pragma once
// The assembly screens: the tree of components and constraints, the
// properties panel, the ribbon commands and what happens in the 3D view
// (picking, dragging components).

#include "app/AppContext.h"
#include "document/StandardParts.h"
#include "kernel/AssemblyModel.h"

#include <filesystem>
#include <optional>

namespace bz::app {

/// New empty assembly in a new tab.
void NewAssembly(AppContext& app);
/// Asks for part files and places them in the active assembly.
void PlaceComponent(AppContext& app);
/// Places `file` in the assembly of `session`, beside what is there (the
/// first one at the origin, fixed). Returns the new component's id.
/// `checkpoint`: remember the assembly first (one undo step); off when the
/// caller made the step for several parts.
ObjectId PlaceFile(AppContext& app, Session& session, const std::filesystem::path& file, bool checkpoint = true);
/// Makes a standard part, saves it next to the assembly (in "Стандартные
/// изделия", reusing a file with the same designation) and places it.
bool PlaceStandardPart(AppContext& app, const doc::parts::Request& request);
/// AutoDrop: the screw `request.standard` (size and length are picked) with
/// a washer and a nut of the matching standards waits for a click on a
/// hole's rim in the active assembly. False when this screw cannot be dropped.
bool StartAutoDrop(AppContext& app, const doc::parts::Request& request, bool washer, bool nut);
/// The screw can be dropped onto a hole (bolts, cap and pan screws).
bool CanAutoDrop(const doc::parts::StandardInfo& standard);

/// The active document is an assembly.
bool InAssembly(AppContext& app);

void DrawAssemblyTree(AppContext& app, Session& session, float width);
void DrawAssemblyProperties(AppContext& app, Session& session, float width);
/// Picking and dragging in the 3D view; overlays.
void DrawAssemblyViewport(AppContext& app, Session& session, const ViewportFrame& vf);

/// Esc / Delete in an assembly. True when handled.
bool AssemblyEscape(AppContext& app, Session& session);
bool AssemblyDelete(AppContext& app, Session& session);

/// Starts making a constraint of this type: pick on two components.
void StartConstraint(AppContext& app, assembly::ConstraintType type);
/// "Массив": repeats the chosen component with the standard parts tied to
/// it — into the holes like the one it sits in, otherwise in a row.
void StartPattern(AppContext& app);
bool CanPattern(AppContext& app);

/// The panel of the constraint being made (over the tree meanwhile: the
/// tree stays below it to pick origin planes and work geometry from).
void DrawConstraintPanel(AppContext& app, Session& session, float width);

/// A pick for the constraint being made (from the view or the tree): the
/// first, second or (symmetry) third geometry; the last one makes it.
/// False when no constraint is being made.
bool TakeConstraintPick(AppContext& app, Session& session, const assembly::GeometryRef& g);
/// A joint picked for a transmission being made (Передача, Винт-рейка).
bool TakeJointPick(AppContext& app, Session& session, ObjectId joint);
/// An origin element or work geometry picked in the tree (of a component,
/// or kAssemblyOrigin): for the constraint or the new part being made.
bool TakeElementPick(AppContext& app, Session& session, ObjectId component, ObjectId element);
/// "Переопределить": the constraint's geometry picked again (and its type
/// changed to `type`, when given); its options are kept.
void StartRedefine(AppContext& app, ObjectId constraint, std::optional<assembly::ConstraintType> type = std::nullopt);

/// "Закрепить" / "Освободить" the selected component.
void ToggleGrounded(AppContext& app);

// --- for commands (scripting): what the clicks above do ----------------------

/// What a pick for a constraint of this type takes (a face, a circular edge, an axis).
kernel::AssemblyPick ConstraintPick(assembly::ConstraintType type);
/// Why `g` cannot be used for this constraint (with `first` already picked,
/// if any; Russian, untranslated), or nullptr when it can.
const char* ConstraintUnfit(assembly::ConstraintType type, const assembly::GeometryRef& g, const assembly::GeometryRef* first);
/// The file of a standard part next to the assembly (made and saved the
/// first time); nullopt with a notification when it cannot be made.
std::optional<std::filesystem::path> StandardPartFileOf(AppContext& app, const Session& session, const doc::parts::Request& request);

} // namespace bz::app
