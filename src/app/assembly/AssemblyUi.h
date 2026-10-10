#pragma once
// The assembly screens: the tree of components and constraints, the
// properties panel, the ribbon commands and what happens in the 3D view
// (picking, dragging components).

#include "app/AppContext.h"
#include "document/StandardParts.h"

#include <filesystem>

namespace bz::app {

/// New empty assembly in a new tab.
void NewAssembly(AppContext& app);
/// Asks for part files and places them in the active assembly.
void PlaceComponent(AppContext& app);
/// Places `file` in the assembly of `session`, beside what is there (the
/// first one at the origin, fixed). Returns the new component's id.
ObjectId PlaceFile(AppContext& app, Session& session, const std::filesystem::path& file);
/// Makes a standard part, saves it next to the assembly (in "Стандартные
/// изделия", reusing a file with the same designation) and places it.
bool PlaceStandardPart(AppContext& app, const doc::parts::Request& request);

/// The active document is an assembly.
bool InAssembly(AppContext& app);

void DrawAssemblyTree(AppContext& app, Session& session, float width);
void DrawAssemblyProperties(AppContext& app, Session& session, float width);
/// Picking and dragging in the 3D view; overlays.
void DrawAssemblyViewport(AppContext& app, Session& session, const ViewportFrame& vf);

/// Esc / Delete in an assembly. True when handled.
bool AssemblyEscape(AppContext& app, Session& session);
bool AssemblyDelete(AppContext& app, Session& session);

/// "Закрепить" / "Освободить" the selected component.
void ToggleGrounded(AppContext& app);

} // namespace bz::app
