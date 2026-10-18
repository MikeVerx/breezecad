#include "app/features/WorkPlaneEditor.h"
#include "app/features/ToolChoice.h"

#include "app/Format.h"
#include "app/features/PointPick.h"
#include "app/viewport/Planes.h"
#include "core/I18n.h"
#include "document/Origin.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

using doc::WorkPlaneMethod;

/// A start (plane or face), for the panel; empty when none.
std::string StartName(const Session& session, ObjectId base, const doc::FaceReference& face) {
    if (face.IsSet()) return Tr("Грань тела");
    if (base == doc::origin::kPlaneXY) return Tr("Плоскость XY");
    if (base == doc::origin::kPlaneXZ) return Tr("Плоскость XZ");
    if (base == doc::origin::kPlaneYZ) return Tr("Плоскость YZ");
    if (const doc::Feature* f = session.doc.Find(base)) return f->name;
    return {};
}

} // namespace

bool WorkPlaneEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::WorkPlaneFeature*>(&f) != nullptr; }

std::string WorkPlaneEditor::Hint() const {
    switch (myMethod) {
        case WorkPlaneMethod::Midplane:
            return Tr("Щёлкните две параллельные плоскости или грани — плоскость встанет посередине. Enter — готово, Esc — отмена");
        case WorkPlaneMethod::Angle:
            return Tr("Щёлкните плоскость или грань, затем прямое ребро на ней, введите угол. Enter — готово, Esc — отмена");
        case WorkPlaneMethod::ThreePoints:
            return Tr("Щёлкните три точки: вершины тела или рабочие точки. Enter — готово, Esc — отмена");
        case WorkPlaneMethod::Tangent:
            return Tr("Щёлкните цилиндрическую грань там, где плоскость должна её касаться. Enter — готово, Esc — отмена");
        case WorkPlaneMethod::Offset: break;
    }
    return Tr("Щёлкните плоскость или плоскую грань, от которой строить, введите смещение. Enter — готово, Esc — отмена");
}

void WorkPlaneEditor::StartNew(AppContext& app, Session& session) {
    auto f = std::make_unique<doc::WorkPlaneFeature>();
    f->offset.name = session.doc.NextParameterName();
    f->angle.name = session.doc.NextParameterName();
    // A plane selected in the tree is the natural start (a plane above a
    // plane); otherwise XY, so the new plane is on screen at once and a
    // click on another plane or a face moves it there.
    const bool planeSelected =
        session.doc.FindAs<doc::WorkPlaneFeature>(session.selectedFeature) || doc::origin::PlaneFrame(session.selectedFeature);
    f->base = planeSelected ? session.selectedFeature : doc::origin::kPlaneXY;
    OpenNew(app, session, std::move(f));
}

void WorkPlaneEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::WorkPlaneFeature&>(feature);
    myOffset = f.offset.expression;
    myAngle = f.angle.expression;
    myMethod = f.method;
    mySlot = 0;
}

void WorkPlaneEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::WorkPlaneFeature*>(Edited(session));
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    myMethod = f->method;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();
    const Vec2 local = vf.ToLocal(mouse);
    // Touching a cylinder: the faces of the body before this plane.
    if (f->method == WorkPlaneMethod::Tangent) {
        app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Faces);
        for (const ShownPlane& p : WorkPlanes(session, static_cast<std::size_t>(index) + 1))
            if (p.id == f->id) DrawPlane(app, dl, vf, p, true);
        myArrow.Release();
        if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
        if (const auto c = app.viewport.PickCylinder(local)) {
            f->cylinder = doc::CylinderTouch{c->axisPoint, c->axisDir, c->radius, c->radial};
            Changed(session);
        } else {
            app.notes.Info(Tr("Нужна цилиндрическая грань"));
        }
        return;
    }
    // Through vertices: only the vertices of the body before this plane.
    if (f->method == WorkPlaneMethod::ThreePoints) {
        for (const ShownPlane& p : WorkPlanes(session, static_cast<std::size_t>(index) + 1))
            if (p.id == f->id) DrawPlane(app, dl, vf, p, true);
        myArrow.Release();
        if (PickPoints(app, session, vf, static_cast<std::size_t>(index), 3, myPointSlot, f->points, f->pointEdges, f->pointFeatures))
            Changed(session);
        return;
    }
    // Through an edge: edges and faces of the body before this plane are picked.
    const bool byEdge = f->method == WorkPlaneMethod::Angle;
    if (byEdge) {
        app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::EdgesAndFaces);
        app.viewport.SetMarkedEdges(f->edge ? std::vector<doc::EdgeReference>{*f->edge} : std::vector<doc::EdgeReference>{});
    }

    // Candidates: origin planes and the work planes before this one.
    std::vector<ShownPlane> planes = OriginPlanes(session);
    const std::vector<ShownPlane> work = WorkPlanes(session, static_cast<std::size_t>(index));
    planes.insert(planes.end(), work.begin(), work.end());
    const ShownPlane* hot = vf.hovered ? PlaneUnderCursor(vf, mouse, planes) : nullptr;
    std::optional<doc::FaceReference> face;
    std::optional<doc::EdgeReference> edge;
    if (vf.hovered && byEdge) {
        edge = app.viewport.PickEdge(local);
        if (!edge) face = app.viewport.PickFace(local);
    } else if (vf.hovered) {
        if (const auto hit = app.viewport.Pick(local); hit && hit->plane) face = doc::FaceReference{hit->plane->normal, hit->point};
    }
    if (face || edge) hot = nullptr;   // the body wins, as when starting a sketch

    const bool faint = !session.model.Bodies().empty();
    for (const ShownPlane& p : planes) {
        const bool chosen = (!f->face.IsSet() && p.id == f->base) ||
                            (f->method == WorkPlaneMethod::Midplane && !f->face2.IsSet() && p.id == f->base2);
        DrawPlane(app, dl, vf, p, &p == hot, faint && &p != hot && !chosen);   // the chosen: not faded
    }
    // The plane being made, in the accent colour.
    bool placed = false;
    for (const ShownPlane& p : WorkPlanes(session, static_cast<std::size_t>(index) + 1)) {
        if (p.id != f->id) continue;
        DrawPlane(app, dl, vf, p, true);
        placed = true;
        if (f->method != WorkPlaneMethod::Offset) continue;
        // Offset arrow from the start to the middle of the new plane.
        const Vec3 dir = p.frame.normal * (f->reversed ? -1.0 : 1.0);
        const Vec3 tip = p.frame.ToWorld(p.centre);
        const std::string label = FormatValue(f->offset.value, Quantity::Length, session.doc.units, 2) + " " +
                                  UnitLabel(Quantity::Length, session.doc.units);
        if (auto offset = myArrow.Update(app, vf, tip - dir * f->offset.value, dir, f->offset.value, label)) {
            const double was = f->reversed ? -f->offset.value : f->offset.value;   // signed, before any flip
            if (*offset < 0) {   // dragged past the start: the other side
                f->reversed = !f->reversed;
                *offset = -*offset;
                myArrow.Flipped();
            }
            myOffset = FormatValue(*offset, Quantity::Length, session.doc.units, 3);
            // The plane moves at once; the part catches up when the rebuild runs.
            if (f->frame) {
                const double now = f->reversed ? -*offset : *offset;
                f->frame = doc::OffsetFrame(*f->frame, now - was);
            }
            f->offset.expression = myOffset;
            f->offset.value = *offset;
            // The part is rebuilt once, on release: rebuilding everything
            // after the plane on every frame of the drag made it crawl.
            myRebuildOnRelease = true;
        }
    }
    if (myRebuildOnRelease && !myArrow.Dragging()) {
        myRebuildOnRelease = false;
        Changed(session);
    }
    if (!placed || f->offset.HasError() || f->method != WorkPlaneMethod::Offset) myArrow.Release();
    if (myArrow.Hot()) return;

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (edge) {
        f->edge = *edge;
        mySlot = 0;
        Changed(session);
        return;
    }
    if (!hot && !face) return;
    const ObjectId id = hot ? hot->id : ObjectId{};
    const doc::FaceReference picked = face ? *face : doc::FaceReference{};
    // The second start of a midplane takes the click when it is the active field.
    if (f->method == WorkPlaneMethod::Midplane && mySlot == 1) {
        f->base2 = id;
        f->face2 = picked;
        mySlot = 0;
    } else {
        f->base = id;
        f->face = picked;
        if (f->method == WorkPlaneMethod::Midplane && !f->base2 && !f->face2.IsSet()) mySlot = 1;
        if (f->method == WorkPlaneMethod::Angle && !f->edge) mySlot = 1;
    }
    Changed(session);
}

void WorkPlaneEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::WorkPlaneFeature&>(feature);
    ui::Ui& ui = app.ui;

    {
        static constexpr std::array<ChoiceItem, 5> methods{{{"wp-offset", N_("Смещение")}, {"wp-mid", N_("Посередине")}, {"wp-angle", N_("Под углом")}, {"wp-3pt", N_("По 3 точкам")}, {"wp-tangent", N_("Касательно к цилиндру")}}};
        const int current = static_cast<int>(f.method);
        if (const int chosen = ToolChoice(app, "method", N_("Способ"), methods, current, width); chosen != current) {
            f.method = static_cast<WorkPlaneMethod>(chosen);
            myMethod = f.method;
            // The start is kept; the next click goes to what the method still needs.
            mySlot = f.method == WorkPlaneMethod::Offset ? 0 : 1;
            Changed(session);
        }
    }
    Gap(app);

    const std::string first = StartName(session, f.base, f.face);
    switch (f.method) {
        case WorkPlaneMethod::Offset:
            Section(app, Tr("От чего строить"));
            PickChip(ui, "##start", Tr("Плоскость"), first, true, width);
            Caption(app, Tr("Щёлкните плоскость начала координат, рабочую плоскость или плоскую грань тела."), width);
            Gap(app);
            Section(app, Tr("Смещение"));
            ParameterField(app, session, "offset", myOffset, f.offset, width, myFocusFirstField);
            break;
        case WorkPlaneMethod::Midplane:
            Section(app, Tr("Между"));
            {
                bool clear1 = false, clear2 = false;
                const char* waiting = Tr("ожидает выбора…");
                if (ui::SelectionSlot(ui, "##first", Tr("Первая"), first, waiting, mySlot == 0, width, &clear1)) mySlot = 0;
                if (ui::SelectionSlot(ui, "##second", Tr("Вторая"), StartName(session, f.base2, f.face2), waiting, mySlot == 1, width,
                                      &clear2))
                    mySlot = 1;
                if (clear1) {
                    f.base = {};
                    f.face = {};
                    mySlot = 0;
                    Changed(session);
                }
                if (clear2) {
                    f.base2 = {};
                    f.face2 = {};
                    mySlot = 1;
                    Changed(session);
                }
            }
            Caption(app, Tr("Две параллельные плоскости или плоские грани. Щелчок в окне попадает в выделенное поле."), width);
            break;
        case WorkPlaneMethod::Angle:
            Section(app, Tr("Через ребро"));
            {
                bool clear1 = false, clear2 = false;
                const char* waiting = Tr("ожидает выбора…");
                if (ui::SelectionSlot(ui, "##first", Tr("Плоскость"), first, waiting, mySlot == 0, width, &clear1)) mySlot = 0;
                if (ui::SelectionSlot(ui, "##edge", Tr("Ребро"), f.edge ? std::string(Tr("выбрано")) : std::string(), waiting, mySlot == 1,
                                      width, &clear2))
                    mySlot = 1;
                if (clear1) {
                    f.base = {};
                    f.face = {};
                    mySlot = 0;
                    Changed(session);
                }
                if (clear2) {
                    f.edge.reset();
                    mySlot = 1;
                    Changed(session);
                }
            }
            Caption(app, Tr("Прямое ребро: новая плоскость проходит через него под углом к исходной. Ребро не должно быть ей перпендикулярно."), width);
            Gap(app);
            Section(app, Tr("Угол"));
            ParameterField(app, session, "angle", myAngle, f.angle, width, myFocusFirstField);
            break;
        case WorkPlaneMethod::Tangent:
            Section(app, Tr("Касательно"));
            PickChip(ui, "##cylinder", Tr("Цилиндр"), f.cylinder ? std::string(Tr("выбран")) : std::string(), true, width);
            Caption(app, Tr("Щёлкните цилиндрическую грань: плоскость коснётся её в этом месте и пойдёт вдоль оси."), width);
            break;
        case WorkPlaneMethod::ThreePoints: {
            Section(app, Tr("Через точки"));
            if (PointSlots(app, 3, myPointSlot, f.points, f.pointEdges, f.pointFeatures, width)) Changed(session);
            Caption(app, Tr("Щёлкните три вершины тела или рабочие точки; щелчок по полю выбирает, какую заменить. Плоскость следует за ними, когда деталь меняется."), width);
            break;
        }
    }
    if (f.method != WorkPlaneMethod::Midplane) {
        Gap(app);
        static constexpr std::array<ChoiceItem, 2> labels{{{"flip-normal", N_("По нормали")}, {"flip-reverse", N_("Обратно")}}};
        const int current = f.reversed ? 1 : 0;
        const int chosen = ToolChoice(app, "direction", N_("Направление"), labels, current, width);
        if (chosen != current) {
            f.reversed = chosen == 1;
            Changed(session);
        }
    }
}

} // namespace bz::app
