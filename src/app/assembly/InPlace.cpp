#include "app/assembly/InPlace.h"

#include "app/assembly/AssemblyUi.h"
#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "app/viewport/ViewPrompt.h"
#include "assembly/AssemblyFile.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Monitor.h"
#include "core/PathText.h"
#include "core/Version.h"
#include "document/BzcFile.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <format>

namespace bz::app {
namespace {

using assembly::Component;
using assembly::Placement;

/// A camera as seen from coordinates moved by `p` (part → assembly: p is the
/// component's placement; assembly → part: its inverse).
render::CameraState Moved(const render::CameraState& c, const Placement& p) {
    render::CameraState out = c;
    out.eye = p.Point(c.eye);
    out.center = p.Point(c.center);
    out.up = p.Direction(c.up).Normalized();
    return out;
}

/// The part session edited in place in the document in front, with its assembly.
struct InPlace {
    Session*            part = nullptr;
    Session*            assembly = nullptr;
    const Component*    component = nullptr;
};

InPlace Current(AppContext& app) {
    InPlace out;
    Session* s = app.Doc();
    if (!s || !s->host || !s->host->assembly || app.workspace.ShowsHome()) return out;
    out.part = s;
    out.assembly = s->host;
    out.component = s->host->assembly->assembly.FindComponent(s->inPlaceOf);
    return out;
}

/// A file name without characters file systems refuse.
std::string SafeName(std::string name) {
    for (char& ch : name)
        if (std::string_view("/\\:*?\"<>|").find(ch) != std::string_view::npos) ch = '_';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? std::string("Деталь") : name;
}

/// The name a new part gets by default: "Деталь1", "Деталь2"... not used yet
/// by a component of this assembly or a part made in it.
std::string NextPartName(const AssemblyState& st) {
    for (int n = 1;; ++n) {
        const std::string name = Trf("Деталь{}", n);
        const bool taken = std::ranges::any_of(st.assembly.components, [&](const Component& c) {
            return c.name.starts_with(name + ":");
        }) || std::ranges::any_of(st.parts, [&](const AssemblyState::PartEdit& e) {
            return e.session && e.session->doc.info.name == name;
        });
        if (!taken) return name;
    }
}

const std::string kGenerator = std::string("BreezeCAD ") + Version();

} // namespace

// --- editing in place -------------------------------------------------------------

bool EditingInPlace(AppContext& app) { return Current(app).part != nullptr; }

bool EditInPlace(AppContext& app, Session& as, ObjectId id, std::string* why) {
    if (!as.assembly) return false;
    AssemblyState& st = *as.assembly;
    const Component* c = st.assembly.FindComponent(id);
    if (!c) return false;
    if (st.inPlace == id && st.InPlaceSession()) return true;
    if (st.inPlace) ReturnToAssembly(app);   // another part was being edited: back first

    // Whatever was going on in the assembly stops (unconfirmed picks are dropped).
    if (OperationEditor* open = app.ops.Active()) open->Cancel(app, as);
    st.creating = {};
    st.dropping = {};
    st.drag = {};
    st.pickingAxis = {};
    st.offer = {};

    const std::filesystem::path file = assembly::ResolvePartPath(as.path, c->path);
    AssemblyState::PartEdit* edit = st.EditOf(file);
    if (!edit) {
        // The same file in a tab of its own would go on with its own copy:
        // whichever is saved last would replace the other's work.
        for (const auto& tab : app.workspace.sessions)
            if (!tab->path.empty() && SameFile(tab->path, file)) {
                const std::string message = Trf("Деталь «{}» открыта во вкладке — закройте её, чтобы править её в сборке",
                                                ToUtf8(file.filename()));
                app.notes.Info(message);
                if (why) *why = message;
                return false;
            }
        std::vector<std::string> warnings;
        Expected<std::unique_ptr<Session>> loaded = Workspace::LoadSession(file, &warnings);
        if (!loaded) {
            const std::string message = Trf("Деталь «{}» не открылась: {}", ToUtf8(file.filename()), Tr(loaded.GetError().message));
            app.notes.Error(message);
            if (why) *why = message;
            return false;
        }
        for (const std::string& w : warnings) app.notes.Info(Tr(w));
        (*loaded)->path = file;   // its name in the tree and the title; it is written with the assembly
        st.parts.push_back({file, std::move(*loaded), false});
        edit = &st.parts.back();
        st.geometry.UsePartInMemory(file, &edit->session->doc, &edit->session->model);
    }
    Session& part = *edit->session;
    part.host = &as;
    part.inPlaceOf = id;
    part.SetMeshQuality(app.settings.graphics);
    part.RebuildNow();   // the assembly shows it from memory from now on
    // The view stays where it was: the assembly's camera seen from the part.
    const render::CameraState camera = as.hasCamera ? as.camera : app.viewport.Camera();
    part.camera = Moved(camera, c->placement.Inverse());
    part.hasCamera = true;
    st.inPlace = id;
    st.selected = {};
    app.shell.ribbonTab = RibbonTab::Model;
    app.shell.leftTab = LeftTab::Tree;
    mon::Action("assembly", "правка на месте: " + c->name);
    log::Info("Editing {} in place in {}", ToUtf8(file.filename()), as.Title());
    return true;
}

void ReturnToAssembly(AppContext& app) {
    const InPlace now = Current(app);
    if (!now.part) return;
    Session& part = *now.part;
    Session& as = *now.assembly;
    // Nothing is thrown away: an open operation is applied, a sketch finished.
    app.ops.Commit(app, part);
    if (part.mode == EditMode::Sketch) app.sketch.Finish(app, part);
    if (part.mode == EditMode::PickPlane) part.mode = EditMode::Model;
    part.RebuildNow();
    if (now.component) {
        as.camera = Moved(part.hasCamera ? part.camera : app.viewport.Camera(), now.component->placement);
        as.hasCamera = true;
        as.assembly->selected = now.component->id;
    }
    part.inPlaceOf = {};
    as.assembly->inPlace = {};
    app.shell.ribbonTab = RibbonTab::Model;
    app.shell.leftTab = LeftTab::Tree;
    mon::Action("assembly", "возврат в сборку");
}

void DrawInPlaceBar(AppContext& app, Session& part, float width) {
    if (!part.host) return;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(74);
    dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accent.WithAlpha(0.08f)));
    dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(c.dividerSoft));
    const std::string where = Trf("Правка в сборке «{}»", part.host->Title());
    ui::FittedText(dl, ui.fonts.strong, ui.S(12), {p.x + ui.S(14), p.y + ui.S(9)}, p.x + width - ui.S(10), ui::ToU32(c.accent),
                   where.c_str());
    ImGui::SetCursorScreenPos({p.x + ui.S(14), p.y + ui.S(32)});
    if (ui::PrimaryButton(ui, "##return-to-assembly", Tr("Вернуться в сборку"), nullptr, {width - ui.S(28), ui.S(32)}))
        app.Later([&app] { ReturnToAssembly(app); });
    ui::ItemTooltip(ui, Tr("Закончить правку детали: остальные компоненты снова доступны. "
                           "Изменения детали сохраняются вместе со сборкой"));
    ImGui::SetCursorScreenPos({p.x, p.y + h});
}

// --- a new part in the assembly ---------------------------------------------------

std::filesystem::path NewPartFile(const Session& session, const std::string& name) {
    const std::string safe = SafeName(name);
    if (session.path.empty()) {
        // Not saved yet: a scratch file; the first save copies it beside the
        // assembly as "<name> - <assembly>.bzc".
        return platform::AutosaveDir() / "new-parts" / session.autosaveKey / PathFromUtf8(safe + ".bzc");
    }
    const std::string stem = ToUtf8(session.path.stem());
    const std::string wanted = std::format("{} - {}.bzc", safe, stem);
    std::filesystem::path file = platform::NewFileBeside(session.path, wanted);
    if (file.empty()) file = session.path.parent_path() / PathFromUtf8(wanted);
    // Not on disk yet, but maybe already given to another new part in memory.
    const AssemblyState& st = *session.assembly;
    for (int n = 2; std::ranges::any_of(st.parts, [&](const AssemblyState::PartEdit& e) { return e.file == file; }); ++n)
        file = session.path.parent_path() / PathFromUtf8(std::format("{} - {} ({}).bzc", safe, stem, n));
    return file;
}

void StartCreatePart(AppContext& app) {
    Session* s = app.Doc();
    if (!s || !s->assembly || app.workspace.ShowsHome()) return;
    AssemblyState& st = *s->assembly;
    if (OperationEditor* open = app.ops.Active()) open->Cancel(app, *s);
    st.creating = {};
    st.dropping = {};
    st.pickingAxis = {};
    st.newPart = {true, NextPartName(st)};
    app.shell.leftTab = LeftTab::Tree;
}

bool CreatingPart(AppContext& app) {
    Session* s = app.Doc();
    return s && s->assembly && s->assembly->newPart.active;
}

ObjectId CreatePartAt(AppContext& app, Session& session, const std::string& typed, const assembly::GeometryRef& plane,
                      bool startSketch) {
    if (!session.assembly) return {};
    AssemblyState& st = *session.assembly;
    std::string name = typed;
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) name = NextPartName(st);
    if (plane.kind != assembly::GeometryKind::Plane) {
        app.notes.Info(Tr("Новая деталь начинается на плоской грани или плоскости"));
        return {};
    }

    // The part's own file, by the rule for parts of assemblies.
    const std::filesystem::path file = NewPartFile(session, name);
    auto part = std::make_unique<Session>();
    part->doc.units.length = static_cast<LengthUnit>(app.settings.newPartLength);
    part->doc.info.name = name;
    part->path = file;
    part->untitledName = name;

    // Its XY plane lies on the picked plane, its Z out of it (the sketch looks
    // at the face, an extrusion grows out of it): what Inventor does. Its
    // origin and X line up with the part it is made on (the foot of that
    // part's origin on the plane, its X laid into the plane): the new
    // sketch's coordinates read as the neighbour's do.
    const Placement at = st.assembly.PlacementOf(plane.component);
    const Vec3 z = at.Direction(plane.direction).Normalized();
    const Vec3 base = at.translation;
    const Vec3 origin = base + z * (at.Point(plane.origin) - base).Dot(z);
    Vec3 x = at.Direction({1, 0, 0});
    x = x - z * x.Dot(z);
    if (x.Length() < 1e-6) x = at.Direction({0, 1, 0}) - z * at.Direction({0, 1, 0}).Dot(z);
    const Placement placed{assembly::Rotation::FromXZ(x.Normalized(), z), origin};

    st.Checkpoint(N_("Создание детали"));
    Component& c = st.assembly.AddComponent(assembly::RelativePartPath(session.path, file), name, placed);
    const ObjectId id = c.id;
    // Held to the plane it was made on (flush), unless it is the assembly's
    // first part: that one is fixed in place anyway.
    if (!c.grounded) {
        assembly::Constraint flush;
        flush.type = assembly::ConstraintType::Mate;
        flush.first = plane;
        flush.second = *assembly::OriginGeometry(id, doc::origin::kPlaneXY);
        flush.flipped = true;   // normals the same way: the part's XY lies on the face
        st.assembly.AddConstraint(flush);
    }
    st.parts.push_back({file, std::move(part), true});
    AssemblyState::PartEdit& edit = st.parts.back();
    edit.session->RebuildNow();
    st.geometry.UsePartInMemory(file, &edit.session->doc, &edit.session->model);
    st.newPart = {};
    mon::Action("assembly", "создание детали: " + name);
    log::Info("Created part {} in {}", ToUtf8(file.filename()), session.Title());

    // Straight into it, with a sketch on its XY plane: draw the first shape.
    if (!EditInPlace(app, session, id) || !startSketch) return id;
    Session* edited = st.InPlaceSession();
    if (!edited) return id;
    const doc::SketchPlacement sketchOn{doc::origin::kPlaneXY, *doc::origin::PlaneFrame(doc::origin::kPlaneXY), false, {}};
    app.Later([&app, edited, sketchOn] {
        if (app.Doc() == edited && edited->mode == EditMode::Model) app.sketch.CreateAndEdit(app, *edited, sketchOn);
    });
    return id;
}

void DrawCreatePartPanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    AssemblyState& st = *session.assembly;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ui.fonts.strong, ui.S(13), {p.x + ui.S(14), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(ui.C().text),
                    Tr("Создать деталь"));
        dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(ui.C().dividerSoft));
        ImGui::Dummy({width, h});
    }
    const float inner = width - ui.S(28);
    ImGui::SetCursorPos({ui.S(14), ImGui::GetCursorPosY() + ui.S(14)});
    ImGui::BeginGroup();
    ui::FieldLabel(ui, Tr("Имя детали"));
    ui::TextField(ui, "##new-part-name", st.newPart.name, inner, Tr("Деталь1"), ImGui::IsWindowAppearing());
    ImGui::Dummy({0, ui.S(8)});
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToU32(ui.C().textSecondary));
    ImGui::TextUnformatted(Tr("Щёлкните по плоской грани компонента — на ней начнётся эскиз новой детали. "
                              "Или выберите плоскость начала координат в дереве."));
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, ui.S(6)});
    {
        const std::string file = ToUtf8(NewPartFile(session, st.newPart.name.empty() ? NextPartName(st) : st.newPart.name).filename());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToU32(ui.C().textSecondary));
        const std::string text = session.path.empty() ? std::string(Tr("Файл детали появится рядом со сборкой при её сохранении"))
                                                      : Trf("Файл: {}", file);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy({0, ui.S(10)});
    if (st.assembly.components.empty()) {
        // An empty assembly: the first part starts on the assembly's XY.
        if (ui::PrimaryButton(ui, "##new-part-xy", Tr("На плоскости XY сборки"), "Enter", {inner, ui.S(32)}) ||
            (ui::EnterPressed(false) && !ImGui::IsAnyItemActive())) {
            const std::string name = st.newPart.name;
            const assembly::GeometryRef xy = *assembly::OriginGeometry(assembly::kAssemblyOrigin, doc::origin::kPlaneXY);
            app.Later([&app, &session, name, xy] { CreatePartAt(app, session, name, xy); });
        }
        ImGui::Dummy({0, ui.S(6)});
    }
    if (ui::SecondaryButton(ui, "##new-part-cancel", Tr("Отмена"), "Esc", {inner, ui.S(32)})) st.newPart = {};
    ImGui::EndGroup();
}

bool CreatePartInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    AssemblyState& st = *session.assembly;
    if (!st.newPart.active) return false;
    DrawViewPrompt(app.ui, ImGui::GetWindowDrawList(), vf,
                   Tr("Создать деталь: щёлкните по плоской грани — на ней начнётся эскиз (Esc — отмена)"),
                   app.ui.C().textSecondary);
    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return true;
    const auto hit = app.viewport.Pick(vf.ToLocal(ImGui::GetMousePos()));
    if (!hit || !hit->body) return true;
    const std::optional<assembly::GeometryRef> g =
        st.geometry.GeometryAt(st.assembly, hit->body, hit->point, kernel::AssemblyPick::Face);
    if (!g || g->kind != assembly::GeometryKind::Plane) {
        app.notes.Info(Tr("Новая деталь начинается на плоской грани или плоскости"));
        return true;
    }
    const std::string name = st.newPart.name;
    const assembly::GeometryRef plane = *g;
    app.Later([&app, &session, name, plane] { CreatePartAt(app, session, name, plane); });
    return true;
}

bool CreatePartPickElement(AppContext& app, Session& session, ObjectId component, ObjectId element) {
    AssemblyState& st = *session.assembly;
    if (!st.newPart.active) return false;
    const std::optional<assembly::GeometryRef> g = st.geometry.ElementGeometry(component, element);
    if (!g || g->kind != assembly::GeometryKind::Plane) {
        app.notes.Info(Tr("Новая деталь начинается на плоской грани или плоскости"));
        return true;
    }
    const std::string name = st.newPart.name;
    const assembly::GeometryRef plane = *g;
    app.Later([&app, &session, name, plane] { CreatePartAt(app, session, name, plane); });
    return true;
}

// --- saving with the assembly -------------------------------------------------------

bool WriteCreatedParts(Session& as, const std::vector<std::vector<ObjectId>>& users, std::string& error) {
    if (!as.assembly) return true;
    for (std::size_t i = 0; i < as.assembly->parts.size(); ++i) {
        AssemblyState::PartEdit& e = as.assembly->parts[i];
        if (!e.created || !e.session) continue;
        // Its component undone (or deleted): no file for it; it stays in
        // memory in case a redo brings the component back.
        if (i >= users.size() || users[i].empty()) continue;
        std::error_code ec;
        std::filesystem::create_directories(e.file.parent_path(), ec);
        e.session->RebuildNow();
        if (const Status s = doc::SaveBzc(e.session->doc, e.file, kGenerator); !s) {
            error = std::format("Деталь «{}» не записана: {}", ToUtf8(e.file.filename()), s.GetError().message);
            return false;
        }
        if (!platform::FileWritten(e.file)) {
            error = std::format("Деталь «{}» не попала в папку проектов", ToUtf8(e.file.filename()));
            return false;
        }
    }
    return true;
}

std::vector<std::vector<ObjectId>> PartUsers(const Session& as, const std::filesystem::path& file) {
    std::vector<std::vector<ObjectId>> out;
    if (!as.assembly) return out;
    for (const AssemblyState::PartEdit& e : as.assembly->parts) {
        std::vector<ObjectId> ids;
        for (const Component& c : as.assembly->assembly.components) {
            const std::filesystem::path p = assembly::ResolvePartPath(file, c.path);
            if (p == e.file || SameFile(p, e.file)) ids.push_back(c.id);
        }
        out.push_back(std::move(ids));
    }
    return out;
}

bool WriteEditedParts(Session& as, const std::vector<std::vector<ObjectId>>& users, const std::filesystem::path& target,
                      std::string& error) {
    if (!as.assembly) return true;
    AssemblyState& st = *as.assembly;
    std::vector<std::size_t> unused;
    for (std::size_t i = 0; i < st.parts.size() && i < users.size(); ++i) {
        AssemblyState::PartEdit& e = st.parts[i];
        // Where its components refer to now: beside the assembly.
        std::optional<std::filesystem::path> now;
        for (ObjectId id : users[i])
            if (const Component* c = st.assembly.FindComponent(id)) {
                now = assembly::ResolvePartPath(target, c->path);
                break;
            }
        if (!now) {
            // No component uses it any more (deleted): nothing to write. A
            // part made here stays in memory (a redo may bring it back);
            // one read from its file is let go (the file still has it).
            if (!e.created && e.session.get() != st.InPlaceSession()) unused.push_back(i);
            e.unused = true;
            continue;
        }
        e.unused = false;
        const bool moved = !(*now == e.file);
        if (e.created || moved || e.session->IsModified()) {
            e.session->RebuildNow();
            if (const Status s = doc::SaveBzc(e.session->doc, *now, kGenerator); !s) {
                error = std::format("Деталь «{}» не записана: {}", ToUtf8(now->filename()), s.GetError().message);
                return false;
            }
            if (!platform::FileWritten(*now)) {
                error = std::format("Деталь «{}» не попала в папку проектов", ToUtf8(now->filename()));
                return false;
            }
            log::Info("Saved part {} with its assembly", ToUtf8(*now));
        }
        if (moved) {
            st.geometry.ForgetPartInMemory(e.file);
            if (e.created) {
                std::error_code ec;
                std::filesystem::remove(e.file, ec);   // the scratch file: its copy lies beside the assembly now
            }
            e.file = *now;
            st.geometry.UsePartInMemory(e.file, &e.session->doc, &e.session->model);
        }
        e.session->path = e.file;
        e.session->MarkSaved();
        e.created = false;
    }
    for (std::size_t k = unused.size(); k-- > 0;) {
        const std::size_t i = unused[k];
        st.geometry.ForgetPartInMemory(st.parts[i].file);
        st.parts.erase(st.parts.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return true;
}

void RenameContext(Session& as, const std::string& from, const std::string& to) {
    if (!as.assembly || from == to) return;
    AssemblyState& st = *as.assembly;
    std::vector<std::filesystem::path> done;
    for (const Component& c : st.assembly.components) {
        const doc::Document* part = st.geometry.PartOf(c.id);
        if (!part) continue;
        bool uses = false;
        for (const auto& f : part->Features())
            if (const auto* sk = dynamic_cast<const doc::SketchFeature*>(f.get()))
                for (const doc::ContextProjection& p : sk->contextProjections) uses = uses || p.assembly == from;
        if (!uses) continue;
        const std::filesystem::path file = assembly::ResolvePartPath(as.path, c.path);
        if (std::find(done.begin(), done.end(), file) != done.end()) continue;
        done.push_back(file);
        AssemblyState::PartEdit* edit = st.EditOf(file);
        if (!edit) {
            Expected<std::unique_ptr<Session>> loaded = Workspace::LoadSession(file, nullptr);
            if (!loaded) continue;
            (*loaded)->path = file;
            st.parts.push_back({file, std::move(*loaded), false});
            edit = &st.parts.back();
            edit->session->RebuildNow();   // shown from memory from now on: built first
            st.geometry.UsePartInMemory(file, &edit->session->doc, &edit->session->model);
        }
        for (const auto& f : edit->session->doc.Features())
            if (auto* sk = dynamic_cast<doc::SketchFeature*>(f.get()))
                for (doc::ContextProjection& p : sk->contextProjections)
                    if (p.assembly == from) p.assembly = to;
        edit->session->history.MarkUnsaved();   // written with the assembly
    }
    st.ForgetAdaptState();
}

bool PartFreeToOpen(Workspace& workspace, Notifications& notes, const std::filesystem::path& file) {
    for (const auto& tab : workspace.sessions) {
        if (!tab->assembly) continue;
        AssemblyState& st = *tab->assembly;
        for (std::size_t i = 0; i < st.parts.size(); ++i) {
            AssemblyState::PartEdit& e = st.parts[i];
            if (!(e.file == file || SameFile(e.file, file))) continue;
            const bool busy = e.created || (e.session && (e.session->IsModified() || e.session.get() == st.InPlaceSession()));
            if (busy) {
                notes.Info(Trf("Деталь «{}» правится в сборке «{}»: сохраните сборку или закончите правку",
                                   ToUtf8(file.filename()), tab->Title()));
                return false;
            }
            // Unchanged: the assembly reads it from its file again.
            st.geometry.ForgetPartInMemory(e.file);
            st.parts.erase(st.parts.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    return true;
}

} // namespace bz::app
