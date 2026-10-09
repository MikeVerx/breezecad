#include "app/Operations.h"

#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "document/BzcFile.h"
#include "document/Commands.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
#include "document/WorkPlaneFeature.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/ViewCube.h"

namespace bz::app {
namespace {

/// Leaves any tool or panel of the active document (before switching away).
void EndEditing(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    if (OperationEditor* open = app.ops.Active()) open->Cancel(app, *s);
    if (s->mode == EditMode::Sketch) app.sketch.Finish(app, *s);
    if (s->mode == EditMode::PickPlane) s->mode = EditMode::Model;
}

} // namespace

void NewDocument(AppContext& app) {
    EndEditing(app);
    Session& s = app.workspace.NewPart();
    s.doc.units.length = static_cast<LengthUnit>(app.settings.newPartLength);
    app.shell.ribbonTab = RibbonTab::Model;
}

void OpenDocument(AppContext& app, std::optional<std::filesystem::path> file) {
    if (!file) {
        const Session* s = app.Doc();
        const std::filesystem::path folder = s && !s->path.empty() ? s->path.parent_path()
                                           : !app.settings.recentFiles.empty() ? app.settings.recentFiles.front().parent_path()
                                                                               : platform::DocumentsDir();
        if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
            app.notes.Error(problem);
            return;
        }
        file = platform::ShowOpenDialog(Tr("Открыть документ"), {{Tr("Документ BreezeCAD"), "*.bzc"}}, folder);
        if (!file) return;
    }
    EndEditing(app);
    app.workspace.Open(*file, app.notes, app.settings);
}

void SaveDocument(AppContext& app, bool saveAs) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    if (app.workspace.Save(*s, saveAs, app.notes, app.settings)) app.autosave.Discard(*s);
}

void ExportDocument(AppContext& app, ExportFormat format) {
    if (Session* s = app.Doc()) app.workspace.Export(*s, format, app.notes);
}

void RequestClose(AppContext& app, int index) {
    if (index < 0 || index >= static_cast<int>(app.workspace.sessions.size())) return;
    Session& s = *app.workspace.sessions[static_cast<std::size_t>(index)];
    if (index == app.workspace.ActiveIndex()) EndEditing(app);
    if (s.IsModified()) {
        app.workspace.Activate(index);
        app.shell.closeRequest = index;
        return;
    }
    app.autosave.Discard(s);
    app.workspace.Close(index);
}

void RecoverDocument(AppContext& app, const RecoverableDocument& rec) {
    Expected<doc::Document> loaded = doc::LoadBzc(rec.copy);
    if (!loaded) {
        app.notes.Error(Trf("Копию не удалось открыть: {}", Tr(loaded.GetError().message)));
        return;
    }
    EndEditing(app);
    Session& s = app.workspace.NewPart();
    s.doc = std::move(*loaded);
    s.path = rec.original;
    s.untitledName = rec.title;
    s.history.MarkUnsaved();   // the recovered state is not in the original file
    app.autosave.Remove(rec);
    std::erase_if(app.shell.recoverable, [&](const RecoverableDocument& r) { return r.folder == rec.folder; });
    app.notes.Success(Trf("Восстановлено: {}. Сохраните документ.", rec.title));
}

bool CanUndo(AppContext& app) {
    Session* s = app.Doc();
    return s && !app.ops.Active() && s->history.CanUndo();
}

bool CanRedo(AppContext& app) {
    Session* s = app.Doc();
    return s && !app.ops.Active() && s->history.CanRedo();
}

void Undo(AppContext& app) {
    if (!CanUndo(app)) return;
    Session& s = *app.Doc();
    s.Undo();
    // The sketch being edited may have disappeared with the undo.
    if (s.mode == EditMode::Sketch && !s.doc.Find(s.activeSketch)) app.sketch.Finish(app, s);
}

void Redo(AppContext& app) {
    if (!CanRedo(app)) return;
    app.Doc()->Redo();
}

void StartSketch(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    if (s->mode == EditMode::Sketch) app.sketch.Finish(app, *s);
    // An origin or work plane selected in the tree: sketch on it right away.
    std::optional<Frame> frame = doc::origin::PlaneFrame(s->selectedFeature);
    if (const auto* wp = s->doc.FindAs<doc::WorkPlaneFeature>(s->selectedFeature); wp && wp->frame) frame = wp->frame;
    if (frame) {
        app.sketch.CreateAndEdit(app, *s, {s->selectedFeature, *frame, false, {}});
        return;
    }
    s->mode = EditMode::PickPlane;
}

void StartExtrude(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.extrude.StartNew(app, *s);
}

void StartFillet(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.fillet.StartNew(app, *s);
}

void StartChamfer(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.chamfer.StartNew(app, *s);
}

void StartHole(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.hole.StartNew(app, *s);
}

void StartWorkPlane(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.workPlane.StartNew(app, *s);
}

void StartWorkAxis(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.workAxis.StartNew(app, *s);
}

void StartThread(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.thread.StartNew(app, *s);
}

void StartPattern(AppContext& app, doc::PatternKind kind) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.pattern.StartNew(app, *s, kind);
}

void StartMirror(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.mirror.StartNew(app, *s);
}

void StartLoft(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.loft.StartNew(app, *s);
}

void StartSweep(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.sweep.StartNew(app, *s);
}

void StartCoil(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.coil.StartNew(app, *s);
}

void StartSection(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.section.StartNew(app, *s);
}

void StartRevolve(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.revolve.StartNew(app, *s);
}

void EditFeature(AppContext& app, Session& session, ObjectId id) {
    const doc::Feature* f = session.doc.Find(id);
    if (!f) return;
    if (session.doc.FindAs<doc::SketchFeature>(id)) {
        app.ops.Close(app, session);
        app.sketch.Edit(app, session, id);
    } else if (OperationEditor* editor = app.ops.EditorFor(*f)) {
        app.ops.Close(app, session);
        editor->EditExisting(app, session, id);
    }
}

void DeleteFeature(AppContext& app, Session& session, ObjectId id) {
    const doc::Feature* f = session.doc.Find(id);
    if (!f) return;
    if (OperationEditor* open = app.ops.Active()) open->Cancel(app, session);
    if (session.mode == EditMode::Sketch && session.activeSketch == id) app.sketch.Finish(app, session);
    session.Execute(doc::RemoveFeature(session.doc, id));
    session.selectedFeature = {};
}

void LookFrom(AppContext& app, Vec3 direction, Vec3 up) {
    // The view looks along -direction; keep the zoom, re-centre on the model.
    render::CameraState target = app.viewport.FitCamera(-direction, up);
    target.scale = app.viewport.Camera().scale;
    app.viewport.AnimateTo(target, app.settings.animations ? 0.42 : 0.0);
}

void HomeView(AppContext& app) {
    const render::CameraState target = app.viewport.FitCamera(-ui::kHomeDirection, {0, 0, 1});
    app.viewport.AnimateTo(target, app.settings.animations ? 0.45 : 0.0);
}

void FitView(AppContext& app) {
    const render::CameraState now = app.viewport.Camera();
    // In a sketch: the sketch and its dimension labels, not the whole model.
    if (Session* s = app.Doc(); s && s->mode == EditMode::Sketch)
        if (const Box3 box = app.sketch.FitBox(*s); !box.IsEmpty()) {
            app.viewport.AnimateTo(app.viewport.FitCamera(now.center - now.eye, now.up, box, 1.15),
                                   app.settings.animations ? 0.35 : 0.0);
            return;
        }
    const render::CameraState target = app.viewport.FitCamera(now.center - now.eye, now.up);
    app.viewport.AnimateTo(target, app.settings.animations ? 0.35 : 0.0);
}

void KeepInView(AppContext& app, const Box3& box) {
    if (box.IsEmpty()) return;
    const ViewProjection proj = app.viewport.Projection();
    bool inside = true;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z};
        const Vec2 px = proj.Project(corner);
        inside = inside && px.x >= 0 && px.y >= 0 && px.x <= proj.width && px.y <= proj.height;
    }
    if (inside) return;
    const render::CameraState now = app.viewport.Camera();
    const render::CameraState target = app.viewport.FitCamera(now.center - now.eye, now.up, box, 1.25);
    app.viewport.AnimateTo(target, app.settings.animations ? 0.35 : 0.0);
}

void EscapePressed(AppContext& app) {
    if (app.shell.paletteOpen) {
        app.shell.paletteOpen = false;
        return;
    }
    Session* s = app.Doc();
    if (!s) return;
    if (OperationEditor* open = app.ops.Active()) {
        open->Cancel(app, *s);
    } else if (app.shell.measuring) {
        app.shell.measuring = false;
    } else if (s->mode == EditMode::Sketch) {
        app.sketch.Escape(app, *s);
    } else if (s->mode == EditMode::PickPlane) {
        s->mode = EditMode::Model;
    } else {
        s->selectedFeature = {};
    }
}

void EnterPressed(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    if (OperationEditor* open = app.ops.Active()) open->Done(app, *s);
}

void DeletePressed(AppContext& app) {
    Session* s = app.Doc();
    if (!s || app.ops.Active()) return;
    if (s->mode == EditMode::Sketch) {
        app.sketch.DeleteSelection(*s);
    } else if (s->selectedFeature && s->doc.Find(s->selectedFeature)) {
        DeleteFeature(app, *s, s->selectedFeature);
    }
}

void SetTheme(AppContext& app, ThemeChoice theme) {
    app.settings.theme = theme;
}

} // namespace bz::app
