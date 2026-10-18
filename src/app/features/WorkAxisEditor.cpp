#include "app/features/WorkAxisEditor.h"
#include "app/features/ToolChoice.h"

#include "app/features/PointPick.h"
#include "app/viewport/Planes.h"
#include "core/I18n.h"
#include "document/Origin.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

using doc::WorkAxisMethod;

std::string PlaneName(const Session& session, ObjectId id) {
    if (id == doc::origin::kPlaneXY) return Tr("Плоскость XY");
    if (id == doc::origin::kPlaneXZ) return Tr("Плоскость XZ");
    if (id == doc::origin::kPlaneYZ) return Tr("Плоскость YZ");
    if (const doc::Feature* f = session.doc.Find(id)) return f->name;
    return {};
}

} // namespace

bool WorkAxisEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::WorkAxisFeature*>(&f) != nullptr; }

std::string WorkAxisEditor::Hint() const {
    switch (myMethod) {
        case WorkAxisMethod::Edge:     return Tr("Щёлкните прямое ребро тела. Enter — готово, Esc — отмена");
        case WorkAxisMethod::Planes:   return Tr("Щёлкните две пересекающиеся плоскости. Enter — готово, Esc — отмена");
        case WorkAxisMethod::Points:   return Tr("Щёлкните две точки: вершины тела или рабочие точки. Enter — готово, Esc — отмена");
        case WorkAxisMethod::Cylinder: break;
    }
    return Tr("Щёлкните цилиндрическую грань: вал или отверстие. Enter — готово, Esc — отмена");
}

void WorkAxisEditor::StartNew(AppContext& app, Session& session) {
    auto f = std::make_unique<doc::WorkAxisFeature>();
    OpenNew(app, session, std::move(f));
    myMethod = WorkAxisMethod::Cylinder;
    mySlot = 0;
}

void WorkAxisEditor::LoadTexts(const doc::Feature& feature) {
    myMethod = static_cast<const doc::WorkAxisFeature&>(feature).method;
    mySlot = 0;
}

void WorkAxisEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::WorkAxisFeature*>(Edited(session));
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    myMethod = f->method;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();
    const Vec2 local = vf.ToLocal(mouse);
    DrawWorkAxes(app, dl, vf, session, static_cast<std::size_t>(index) + 1, f->id);
    const bool click = vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    switch (f->method) {
        case WorkAxisMethod::Edge:
            app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Edges);
            app.viewport.SetMarkedEdges(f->edge ? std::vector<doc::EdgeReference>{*f->edge} : std::vector<doc::EdgeReference>{});
            if (click)
                if (const auto e = app.viewport.PickEdge(local)) {
                    f->edge = *e;
                    Changed(session);
                }
            break;
        case WorkAxisMethod::Cylinder:
            app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Faces);
            if (click) {
                if (const auto c = app.viewport.PickCylinder(local)) {
                    f->cylinder = doc::CylinderTouch{c->axisPoint, c->axisDir, c->radius, c->radial};
                    Changed(session);
                } else {
                    app.notes.Info(Tr("Нужна цилиндрическая грань"));
                }
            }
            break;
        case WorkAxisMethod::Points:
            if (PickPoints(app, session, vf, static_cast<std::size_t>(index), 2, myPointSlot, f->points, f->pointEdges, f->pointFeatures))
                Changed(session);
            break;
        case WorkAxisMethod::Planes: {
            std::vector<ShownPlane> planes = OriginPlanes(session);
            const std::vector<ShownPlane> work = WorkPlanes(session, static_cast<std::size_t>(index));
            planes.insert(planes.end(), work.begin(), work.end());
            const ShownPlane* hot = vf.hovered ? PlaneUnderCursor(vf, mouse, planes) : nullptr;
            for (const ShownPlane& p : planes) {
                const bool chosen = p.id == f->plane1 || p.id == f->plane2;
                DrawPlane(app, dl, vf, p, &p == hot || chosen, !chosen && &p != hot);
            }
            if (click && hot) {
                (mySlot == 0 ? f->plane1 : f->plane2) = hot->id;
                mySlot = mySlot == 0 ? 1 : 0;
                Changed(session);
            }
            break;
        }
    }
}

void WorkAxisEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::WorkAxisFeature&>(feature);
    ui::Ui& ui = app.ui;
    static constexpr std::array<ChoiceItem, 4> methods{{{nullptr, N_("Ось цилиндра")}, {nullptr, N_("По ребру")}, {nullptr, N_("Две плоскости")}, {nullptr, N_("Две точки")}}};
    // The panel shows the methods in this order; the enum is Edge, Cylinder, Planes, Points.
    static constexpr std::array<WorkAxisMethod, 4> order{WorkAxisMethod::Cylinder, WorkAxisMethod::Edge, WorkAxisMethod::Planes,
                                                         WorkAxisMethod::Points};
    int current = 0;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (order[i] == f.method) current = static_cast<int>(i);
    if (const int chosen = ToolChoice(app, "method", N_("Способ"), methods, current, width); chosen != current) {
        f.method = order[static_cast<std::size_t>(chosen)];
        myMethod = f.method;
        mySlot = 0;
        Changed(session);
    }
    Gap(app);
    switch (f.method) {
        case WorkAxisMethod::Cylinder:
            PickChip(ui, "##cylinder", Tr("Цилиндр"), f.cylinder ? std::string(Tr("выбран")) : std::string(), true, width);
            Caption(app, Tr("Щёлкните вал или стенку отверстия: ось пройдёт по его центру и будет следовать за ним."), width);
            break;
        case WorkAxisMethod::Edge:
            PickChip(ui, "##edge", Tr("Ребро"), f.edge ? std::string(Tr("выбрано")) : std::string(), true, width);
            Caption(app, Tr("Щёлкните прямое ребро тела."), width);
            break;
        case WorkAxisMethod::Planes:
            {
                bool clear1 = false, clear2 = false;
                const char* waiting = Tr("ожидает выбора…");
                if (ui::SelectionSlot(ui, "##first", Tr("Первая"), PlaneName(session, f.plane1), waiting, mySlot == 0, width, &clear1))
                    mySlot = 0;
                if (ui::SelectionSlot(ui, "##second", Tr("Вторая"), PlaneName(session, f.plane2), waiting, mySlot == 1, width, &clear2))
                    mySlot = 1;
                if (clear1) {
                    f.plane1 = {};
                    mySlot = 0;
                    Changed(session);
                }
                if (clear2) {
                    f.plane2 = {};
                    mySlot = 1;
                    Changed(session);
                }
            }
            Caption(app, Tr("Плоскости начала координат или рабочие; ось — линия их пересечения."), width);
            break;
        case WorkAxisMethod::Points: {
            if (PointSlots(app, 2, myPointSlot, f.points, f.pointEdges, f.pointFeatures, width)) Changed(session);
            Caption(app, Tr("Щёлкните две вершины тела или рабочие точки; щелчок по полю выбирает, какую заменить."), width);
            break;
        }
    }
}

} // namespace bz::app
