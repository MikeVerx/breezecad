#include "app/Operations.h"

#include "app/Format.h"
#include "app/features/OperationEditors.h"
#include "app/mobile/PhoneHome.h"
#include "app/sketch/SketchEditor.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "document/BzcFile.h"
#include "document/Commands.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
#include "app/assembly/AssemblyUi.h"
#include "document/DocumentTypes.h"
#include "document/StandardParts.h"
#include "document/WorkPlaneFeature.h"
#include "modules/ModuleManager.h"
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

void InstallModule(AppContext& app, std::optional<std::filesystem::path> package) {
    if (!app.modules) return;
    if (!package) {
        if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
            app.notes.Error(problem);
            return;
        }
        package = platform::ShowOpenDialog(Tr("Установить модуль"), {{Tr("Модуль BreezeCAD"), "*.bzmod *.zip"}}, platform::DocumentsDir());
        if (!package) return;
    }
    const Expected<std::string> id = app.modules->Install(*package);
    if (!id) {
        app.notes.Error(Trf("Модуль не установлен: {}", Tr(id.GetError().message)));
        return;
    }
    const modules::ModuleEntry* m = app.modules->Find(*id);
    if (m && m->state == modules::ModuleState::Loaded)
        app.notes.Success(Trf("Модуль «{}» {} установлен и работает", m->manifest.name, m->manifest.version));
    else if (m)
        app.notes.Error(Trf("Модуль «{}» установлен, но не запустился: {}", m->manifest.name, Tr(m->problem)));
    app.shell.showSettings = true;   // there it is, with its switch
    app.shell.settingsTab = 3;   // "Модули"
}

void OpenDropped(AppContext& app, const std::filesystem::path& file) {
    std::u8string ext = file.extension().u8string();
    for (char8_t& ch : ext)
        if (ch >= u8'A' && ch <= u8'Z') ch = static_cast<char8_t>(ch - u8'A' + u8'a');
    if (ext == u8".bzmod" || ext == u8".zip") InstallModule(app, file);
    else OpenDocument(app, file);
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
        file = platform::ShowOpenDialog(Tr("Открыть документ"), {{Tr("Документ BreezeCAD"), doc::DocumentTypeRegistry::Default().OpenPatterns()}}, folder);
        if (!file) return;
    }
    EndEditing(app);
    app.workspace.Open(*file, app.notes, app.settings);
}

void SaveDocument(AppContext& app, bool saveAs) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    if (app.workspace.Save(*s, saveAs, app.notes, app.settings)) {
        app.autosave.Discard(*s);
        if (app.shell.mobile.phone) mobile::SaveThumbnail(app, *s);   // the card's picture on the phone's home page
    }
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
    Expected<std::unique_ptr<Session>> loaded = Workspace::LoadSession(rec.copy, nullptr);
    if (!loaded) {
        app.notes.Error(Trf("Копию не удалось открыть: {}", Tr(loaded.GetError().message)));
        return;
    }
    EndEditing(app);
    app.workspace.sessions.push_back(std::move(*loaded));
    app.workspace.Activate(static_cast<int>(app.workspace.sessions.size()) - 1);
    Session& s = *app.workspace.sessions.back();
    s.path = rec.original;
    s.untitledName = rec.title;
    // The recovered state is not in the original file.
    if (s.assembly) s.assembly->MarkUnsaved();
    else s.history.MarkUnsaved();
    app.autosave.Remove(rec);
    std::erase_if(app.shell.recoverable, [&](const RecoverableDocument& r) { return r.folder == rec.folder; });
    app.notes.Success(Trf("Восстановлено: {}. Сохраните документ.", rec.title));
}

bool CanUndo(AppContext& app) {
    Session* s = app.Doc();
    return s && !app.ops.Active() && s->CanUndo();
}

bool CanRedo(AppContext& app) {
    Session* s = app.Doc();
    return s && !app.ops.Active() && s->CanRedo();
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

bool CreateStandardPart(AppContext& app, const doc::parts::Request& request) {
    Expected<doc::parts::Made> made = doc::parts::MakePart(request);
    if (!made) {
        app.notes.Error(Tr(made.GetError().message));
        return false;
    }
    EndEditing(app);
    Session& s = app.workspace.NewPart();
    s.untitledName = made->doc.info.designation;
    s.doc = std::move(made->doc);
    s.doc.units.length = static_cast<LengthUnit>(app.settings.newPartLength);
    s.history.MarkUnsaved();
    app.shell.ribbonTab = RibbonTab::Model;
    return true;
}

void StartWorkPoint(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.workPoint.StartNew(app, *s);
}

void StartShell(AppContext& app) {
    Session* s = app.Doc();
    if (!s) return;
    app.ops.Close(app, *s);
    app.ops.shell.StartNew(app, *s);
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

void ToggleInterference(AppContext& app) {
    app.shell.interference = !app.shell.interference;
    Session* s = app.Doc();
    if (!app.shell.interference || !s) return;
    const auto& bodies = s->model.Bodies();
    if (bodies.size() < 2) {
        app.notes.Info(Tr(s->assembly ? "В сборке один компонент — пересекаться не с чем" : "В детали одно тело — пересекаться не с чем"));
        return;
    }
    const std::vector<kernel::Interference>& found = s->model.Interferences();
    if (found.empty()) {
        app.notes.Success(Tr(s->assembly ? "Компоненты не пересекаются" : "Тела не пересекаются"));
        return;
    }
    const UnitSystem& units = s->doc.units;
    const double k = units.ToDisplay(1.0, Quantity::Length);
    std::string list;
    for (const kernel::Interference& i : found) {
        if (!list.empty()) list += "; ";
        list += Trf("{} и {}: {} {}\xC2\xB3", bodies[i.first].name, bodies[i.second].name, FormatGrouped(i.volume * k * k * k, 2),
                    UnitLabel(Quantity::Length, units));
    }
    app.notes.Error(Trf("Пересечения (показаны красным): {}", list));
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
    } else if (s->assembly && AssemblyEscape(app, *s)) {
    } else if (!s->selectedFaces.empty()) {
        s->selectedFaces.clear();   // first the faces chosen in the view, then the tree
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
    } else if (s->assembly) {
        AssemblyDelete(app, *s);
    } else if (s->selectedFeature && s->doc.Find(s->selectedFeature)) {
        DeleteFeature(app, *s, s->selectedFeature);
    }
}

void SetTheme(AppContext& app, ThemeChoice theme) {
    app.settings.theme = theme;
}

} // namespace bz::app
