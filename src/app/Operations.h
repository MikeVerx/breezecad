#pragma once
// User-level operations shared by actions, menus, panels and the viewport.

#include "document/StandardParts.h"

#include "app/AppContext.h"
#include "document/PatternFeature.h"

#include <filesystem>
#include <optional>

namespace bz::app {

void NewDocument(AppContext& app);
/// Opens the "Создать" window on the template used last (NewDocumentDialog.cpp).
void OpenNewDocumentDialog(AppContext& app);
/// Opens `file`, or shows the open dialog when none is given.
void OpenDocument(AppContext& app, std::optional<std::filesystem::path> file = {});
void SaveDocument(AppContext& app, bool saveAs);
/// Installs a module package (.bzmod), or asks for one when none is given;
/// says how it went and shows it in "Настройки → Модули".
void InstallModule(AppContext& app, std::optional<std::filesystem::path> package = {});
/// Loads the packs of standard parts (doc::parts, StandardPacks.h): the
/// user's folder first (a pack installed there replaces the shipped copy of
/// it), then the shipped ones. Packs that could not be read are reported.
void LoadStandardPacks(AppContext& app);
/// Adds a pack of standard parts (.bzlib) to the user's library folder, or
/// asks for one when none is given; reloads the packs and says how it went.
void InstallStandardPack(AppContext& app, std::optional<std::filesystem::path> file = {});
/// The user's folder of packs of standard parts.
std::filesystem::path StandardPackDir();
/// A file dropped on the window: a module package is installed, a pack of
/// standard parts added to the library, anything else opened as a document.
void OpenDropped(AppContext& app, const std::filesystem::path& file);
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
void StartShell(AppContext& app);
void StartWorkPoint(AppContext& app);
/// Builds a standard part (doc::parts) and opens it as a new document;
/// reports what went wrong otherwise. True on success.
bool CreateStandardPart(AppContext& app, const doc::parts::Request& request);
void StartHole(AppContext& app);
void StartWorkPlane(AppContext& app);
void StartWorkAxis(AppContext& app);
void StartThread(AppContext& app);
/// "Коллизии": shows or hides the overlaps of the bodies and says what was found.
void ToggleInterference(AppContext& app);
void StartPattern(AppContext& app, doc::PatternKind kind);
void StartMirror(AppContext& app);
void StartLoft(AppContext& app);
void StartSweep(AppContext& app);
void StartCoil(AppContext& app);
void StartSection(AppContext& app);
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
