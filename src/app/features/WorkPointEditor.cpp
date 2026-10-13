#include "app/features/WorkPointEditor.h"

#include "app/viewport/Planes.h"
#include "core/I18n.h"
#include "document/Origin.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

using doc::WorkPointMethod;

std::string NameOf(const Session& session, ObjectId id) {
    if (id == doc::origin::kPlaneXY) return Tr("Плоскость XY");
    if (id == doc::origin::kPlaneXZ) return Tr("Плоскость XZ");
    if (id == doc::origin::kPlaneYZ) return Tr("Плоскость YZ");
    if (const doc::Feature* f = session.doc.Find(id)) return f->name;
    return {};
}

} // namespace

bool WorkPointEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::WorkPointFeature*>(&f) != nullptr; }

std::string WorkPointEditor::Hint() const {
    switch (myMethod) {
        case WorkPointMethod::Vertex:       return Tr("Щёлкните вершину тела. Enter — готово, Esc — отмена");
        case WorkPointMethod::Center:       return Tr("Щёлкните круглое ребро (окружность, дугу) или сферу. Enter — готово, Esc — отмена");
        case WorkPointMethod::Midpoint:     return Tr("Щёлкните ребро. Enter — готово, Esc — отмена");
        case WorkPointMethod::Intersection: return Tr("Щёлкните прямое ребро или рабочую ось, затем плоскость. Enter — готово, Esc — отмена");
        case WorkPointMethod::Coordinates:  break;
    }
    return Tr("Введите координаты X, Y, Z. Enter — готово, Esc — отмена");
}

bool WorkPointEditor::ShowStatus(const doc::Feature& feature) const {
    // "Щёлкните …" before anything is picked repeats the caption.
    const auto& f = static_cast<const doc::WorkPointFeature&>(feature);
    switch (f.method) {
        case WorkPointMethod::Center:       return f.edge.has_value() || f.sphere.has_value();
        case WorkPointMethod::Intersection: return (f.edge.has_value() || f.axis) && f.plane;
        case WorkPointMethod::Coordinates:  return true;
        default:                            return f.edge.has_value();
    }
}

void WorkPointEditor::StartNew(AppContext& app, Session& session) {
    auto f = std::make_unique<doc::WorkPointFeature>();
    // With no body there is nothing to click: start with coordinates.
    if (session.model.Bodies().empty()) f->method = WorkPointMethod::Coordinates;
    myMethod = f->method;
    mySlot = 0;
    OpenNew(app, session, std::move(f));
}

void WorkPointEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::WorkPointFeature&>(feature);
    myMethod = f.method;
    mySlot = 0;
    myX = f.x.expression;
    myY = f.y.expression;
    myZ = f.z.expression;
}

void WorkPointEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::WorkPointFeature*>(Edited(session));
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    const auto before = static_cast<std::size_t>(index);
    myMethod = f->method;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();
    const Vec2 local = vf.ToLocal(mouse);
    // The other work points faint, this one strong.
    DrawWorkPoints(app, dl, vf, WorkPoints(session, before + 1), f->id);
    const bool click = vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const auto marked = [&] {
        app.viewport.SetMarkedEdges(f->edge ? std::vector<doc::EdgeReference>{*f->edge} : std::vector<doc::EdgeReference>{});
    };

    switch (f->method) {
        case WorkPointMethod::Vertex:
            app.viewport.BeginPickingOriginal(session.model, before, render::PickTarget::Vertices);
            if (click)
                if (const auto v = app.viewport.PickVertex(local)) {
                    f->vertex = v->point;
                    f->edge = v->edge;
                    Changed(session);
                }
            break;
        case WorkPointMethod::Center:
            app.viewport.BeginPickingOriginal(session.model, before, render::PickTarget::EdgesAndFaces);
            marked();
            if (!click) break;
            if (const auto e = app.viewport.PickEdge(local)) {
                if (e->curve == doc::CurveKind::Line) {
                    app.notes.Info(Tr("У прямого ребра нет центра: нужна окружность, дуга или сфера"));
                } else {
                    f->edge = *e;
                    f->sphere.reset();
                    Changed(session);
                }
            } else if (const auto face = app.viewport.PickFace(local, true); face && face->curved) {
                f->sphere = *face;
                f->edge.reset();
                Changed(session);
            }
            break;
        case WorkPointMethod::Midpoint:
            app.viewport.BeginPickingOriginal(session.model, before, render::PickTarget::Edges);
            marked();
            if (click)
                if (const auto e = app.viewport.PickEdge(local)) {
                    f->edge = *e;
                    Changed(session);
                }
            break;
        case WorkPointMethod::Intersection: {
            app.viewport.BeginPickingOriginal(session.model, before, render::PickTarget::Edges);
            if (!f->axis) marked();
            const ObjectId axisHot = mySlot == 0 && vf.hovered ? AxisUnderCursor(app, vf, mouse, session, before) : ObjectId{};
            DrawWorkAxes(app, dl, vf, session, before, axisHot ? axisHot : f->axis);
            std::vector<ShownPlane> planes = OriginPlanes(session);
            const std::vector<ShownPlane> work = WorkPlanes(session, before);
            planes.insert(planes.end(), work.begin(), work.end());
            const ShownPlane* hot = mySlot == 1 && vf.hovered ? PlaneUnderCursor(vf, mouse, planes) : nullptr;
            for (const ShownPlane& p : planes) {
                const bool chosen = p.id == f->plane;
                DrawPlane(app, dl, vf, p, &p == hot || chosen, !chosen && &p != hot);
            }
            if (!click) break;
            if (mySlot == 0) {
                if (axisHot) {
                    f->axis = axisHot;
                    f->edge.reset();
                } else if (const auto e = app.viewport.PickEdge(local)) {
                    f->edge = *e;
                    f->axis = {};
                } else {
                    break;
                }
                mySlot = 1;
                Changed(session);
            } else if (hot) {
                f->plane = hot->id;
                mySlot = 0;
                Changed(session);
            }
            break;
        }
        case WorkPointMethod::Coordinates: break;
    }
}

void WorkPointEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::WorkPointFeature&>(feature);
    ui::Ui& ui = app.ui;
    Section(app, Tr("Способ"));
    static constexpr std::array<const char*, 5> methods{N_("Вершина"), N_("Центр"), N_("Середина"), N_("Пересечение"),
                                                        N_("Координаты")};
    const int current = static_cast<int>(f.method);
    if (const int chosen = ui::Segmented(ui, "method", methods, current, 3, width); chosen != current && chosen >= 0) {
        f.method = static_cast<WorkPointMethod>(chosen);
        myMethod = f.method;
        mySlot = 0;
        Changed(session);
    }
    Gap(app);
    const auto chosenText = [](bool set, const char* yes) { return set ? std::string(Tr(yes)) : std::string(); };
    switch (f.method) {
        case WorkPointMethod::Vertex:
            PickChip(ui, "##vertex", Tr("Вершина"), chosenText(f.edge.has_value(), N_("выбрана")), true, width);
            Caption(app, Tr("Щёлкните вершину тела: точка будет следовать за ней, когда деталь меняется."), width);
            break;
        case WorkPointMethod::Center:
            PickChip(ui, "##center", f.sphere ? Tr("Сфера") : Tr("Ребро"),
                     chosenText(f.edge.has_value() || f.sphere.has_value(), N_("выбрано")), true, width);
            Caption(app, Tr("Щёлкните окружность, дугу или эллипс (кромку отверстия, вала) или сферическую грань."), width);
            break;
        case WorkPointMethod::Midpoint:
            PickChip(ui, "##edge", Tr("Ребро"), chosenText(f.edge.has_value(), N_("выбрано")), true, width);
            Caption(app, Tr("Щёлкните ребро: точка встанет посередине его длины."), width);
            break;
        case WorkPointMethod::Intersection: {
            const std::string line = f.axis ? NameOf(session, f.axis) : chosenText(f.edge.has_value(), N_("ребро"));
            if (PickChip(ui, "##line", Tr("Линия"), line, mySlot == 0, width)) mySlot = 0;
            ImGui::Dummy({0, ui.S(4)});
            if (PickChip(ui, "##plane", Tr("Плоскость"), NameOf(session, f.plane), mySlot == 1, width)) mySlot = 1;
            Caption(app, Tr("Прямое ребро или рабочая ось и плоскость начала координат или рабочая; точка — где линия её пересекает."),
                    width);
            break;
        }
        case WorkPointMethod::Coordinates:
            Section(app, "X");
            ParameterField(app, session, "x", myX, f.x, width, myFocusFirstField);
            Section(app, "Y");
            ParameterField(app, session, "y", myY, f.y, width, false);
            Section(app, "Z");
            ParameterField(app, session, "z", myZ, f.z, width, false);
            Caption(app, Tr("Координаты в системе начала координат детали."), width);
            break;
    }
}

} // namespace bz::app
