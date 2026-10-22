#pragma once
// Designing in the assembly, as in Inventor: a part edited in place (double
// click on a component), a new part made right in the assembly. The part's
// own session is kept by the assembly (AssemblyState::parts); while it is
// edited, Workspace::Active() gives that session, so every part tool works
// on it unchanged, and the rest of the assembly stands faded around it.

#include "app/AppContext.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bz::app {

/// The part of `component` edited in place in the assembly `assembly`.
/// False (with a notification) when it cannot be: the part's file is open
/// in a tab, or cannot be read.
bool EditInPlace(AppContext& app, Session& assembly, ObjectId component);
/// The document in front is a part edited in place.
bool EditingInPlace(AppContext& app);
/// Back to the assembly: an open sketch is finished first (an open operation
/// is applied). The part's changes stay in memory until the assembly is saved.
void ReturnToAssembly(AppContext& app);

/// The strip over the tree of a part edited in place: whose part it is and
/// «Вернуться».
void DrawInPlaceBar(AppContext& app, Session& part, float width);

/// "Создать": a new part made in the active assembly. Asks for its name in
/// the left panel, then for a face or plane its sketch starts on.
void StartCreatePart(AppContext& app);
bool CreatingPart(AppContext& app);
/// The panel of "Создать" (replaces the tree meanwhile).
void DrawCreatePartPanel(AppContext& app, Session& session, float width);
/// Its picks in the 3D view; true while it takes the clicks.
bool CreatePartInViewport(AppContext& app, Session& session, const ViewportFrame& vf);
/// Picks in the tree: an origin plane of the assembly or of a component
/// (`component` kAssemblyOrigin for the assembly's own). True when taken.
bool CreatePartPickElement(AppContext& app, Session& session, ObjectId component, ObjectId element);
/// Makes the part at once (name, plane, flush constraint) and opens it in
/// place with a sketch on its XY plane — what the panel does on its last
/// pick; for commands too. Returns the new component, none on failure.
ObjectId CreatePartAt(AppContext& app, Session& session, const std::string& name, const assembly::GeometryRef& plane);

/// The file a new part named `name` gets in the assembly `session`: beside a
/// saved assembly "<name> - <assembly>.bzc" (the rule for parts of
/// assemblies), a new one's in a scratch folder until it is first saved
/// (then it is copied beside it under that name).
std::filesystem::path NewPartFile(const Session& session, const std::string& name);

/// Saving an assembly with parts edited in it (Workspace::SaveTo calls these):
/// the parts made in it written into their own files first (so their copies
/// beside the assembly can be made)…
bool WriteCreatedParts(Session& assembly, std::string& error);
/// Which components use each part edited in the assembly (parallel to
/// AssemblyState::parts) while the assembly is at `file` — taken before the
/// parts are gathered beside it, which changes their paths…
std::vector<std::vector<ObjectId>> PartUsers(const Session& assembly, const std::filesystem::path& file);
/// …then, once every part lies beside the assembly at `target`, the edited
/// ones written into those files (the copies), never into the originals
/// elsewhere.
bool WriteEditedParts(Session& assembly, const std::vector<std::vector<ObjectId>>& users,
                      const std::filesystem::path& target, std::string& error);

/// The assembly is saved under another name (its first save, «Сохранить
/// как»): projections its parts took from each other say the new name, or
/// they would stop following. Parts that have such projections are taken
/// into memory to be written with the assembly.
void RenameContext(Session& assembly, const std::string& from, const std::string& to);

/// A part file about to be opened in a tab: an assembly holding it in memory
/// with changes not saved says so (and it is not opened); one holding it
/// without changes lets it go (it is read from its file again).
bool PartFreeToOpen(Workspace& workspace, Notifications& notes, const std::filesystem::path& file);

} // namespace bz::app
