#pragma once
// «Редактор DXF»: getting a blank ready for the laser. Opens any ASCII DXF
// (or the part's flat pattern), shows it by layers, and lets the user select,
// delete, move, rotate and mirror, move things to another layer (Рез / Гиб /
// Маркировка), add marking text, measure, find open ends and duplicates, and
// save DXF R12 that AutoCAD opens. The drawing model is src/sheetmetal/DxfDrawing.h.

#include "sheetmetal/DxfDrawing.h"

#include <filesystem>
#include <string>

namespace bz::app {

struct AppContext;

/// Opens the editor on `drawing` (the flat pattern, say). `path`: where
/// «Сохранить» writes; empty — it asks.
void OpenDxfEditor(AppContext& app, dxf::Drawing drawing, std::string title, std::filesystem::path path = {});
/// Opens a DXF file in the editor; false (and `error`) when it cannot be read.
bool OpenDxfEditorFile(AppContext& app, const std::filesystem::path& path, std::string* error = nullptr);
/// The window, every frame.
void DrawDxfEditor(AppContext& app);

/// For commands (app/scripting): the drawing open in the editor, nullptr when closed.
dxf::Drawing* DxfEditorDrawing();
/// Saves what the editor has to `path`; false (and `error`) when it cannot.
bool SaveDxfEditor(AppContext& app, const std::filesystem::path& path, std::string* error = nullptr);
/// Selects every entity on `layer` (all when empty), for commands.
std::size_t SelectInDxfEditor(std::string_view layer);
/// Runs «Проверить для резки» in the open editor (the marks show on the drawing).
dxf::Check CheckInDxfEditor();

} // namespace bz::app
