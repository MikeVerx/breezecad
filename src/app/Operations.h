#pragma once
// User-level operations shared by actions, menus, panels and the viewport.

#include "app/AppContext.h"

#include <filesystem>
#include <optional>

namespace bz::app {

void NewDocument(AppContext& app);
/// Opens `file`, or shows the open dialog when none is given.
void OpenDocument(AppContext& app, std::optional<std::filesystem::path> file = {});
void SaveDocument(AppContext& app, bool saveAs);
void ExportDocument(AppContext& app, ExportFormat format);
/// Closes a tab; asks about unsaved changes first.
void RequestClose(AppContext& app, int index);
/// Opens an autosaved copy as an unsaved document.
void RecoverDocument(AppContext& app, const RecoverableDocument& doc);

void Undo(AppContext& app);
void Redo(AppContext& app);
bool CanUndo(AppContext& app);
bool CanRedo(AppContext& app);

/// "Начать эскиз": on the selected plane/face, otherwise asks to pick one.
void StartSketch(AppContext& app);
void StartExtrude(AppContext& app);
void StartRevolve(AppContext& app);
void StartFillet(AppContext& app);
void StartChamfer(AppContext& app);
void StartHole(AppContext& app);
void StartShell(AppContext& app);
/// Double click in the tree: edit a sketch or an operation.
void EditFeature(AppContext& app, Session& session, ObjectId id);
void DeleteFeature(AppContext& app, Session& session, ObjectId id);

void LookFrom(AppContext& app, Vec3 direction, Vec3 up);
void HomeView(AppContext& app);
void FitView(AppContext& app);
/// Zooms out, keeping the view direction, when part of `box` (model
/// coordinates) is off screen. Does nothing when it already fits.
void KeepInView(AppContext& app, const Box3& box);

/// Keys with meaning that depends on what is going on.
void EscapePressed(AppContext& app);
void EnterPressed(AppContext& app);
void DeletePressed(AppContext& app);

void SetTheme(AppContext& app, ThemeChoice theme);

} // namespace bz::app
