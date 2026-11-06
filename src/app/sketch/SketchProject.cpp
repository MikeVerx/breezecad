// «Проецировать геометрию» up to Inventor's level (ТЗ 06.11.2026): model
// edges, face outlines (loops that overlap on the plane trimmed), bodies of
// revolution, vertices (points), work geometry and the origin, curves and
// points of other sketches, edges of other parts in an assembly; the cut of
// the part by the sketch plane («Проецировать сечение», an independent copy
// as Inventor's Project Cut Edges); a sheet metal flat pattern
// («Проецировать развёртку»); edges projected by themselves while drawing
// («Автопроецирование рёбер при рисовании»).

#include "app/Format.h"
#include "app/sketch/ProjectInto.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "app/viewport/Planes.h"
#include "core/I18n.h"
#include "document/Origin.h"
#include "document/SheetMetalFeature.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/WorkPointFeature.h"
#include "kernel/ContextProjection.h"
#include "kernel/ProjectionSources.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

namespace bz::app {
namespace {

/// Sketches above `index` the user sees: not hidden, not suppressed, not the active one.
std::vector<const doc::SketchFeature*> VisibleSketches(const Session& session, std::size_t index, ObjectId active) {
    std::vector<const doc::SketchFeature*> out;
    for (std::size_t i = 0; i < std::min(index, session.doc.Features().size()); ++i) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(session.doc.Features()[i].get());
        if (sk && sk->id != active && !sk->hidden && !sk->suppressed && session.doc.IsActive(i)) out.push_back(sk);
    }
    return out;
}

/// Screen distance from `mouse` to a segment.
float SegmentDistance(ImVec2 mouse, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((mouse.x - a.x) * dx + (mouse.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float px = a.x + t * dx - mouse.x, py = a.y + t * dy - mouse.y;
    return std::sqrt(px * px + py * py);
}

/// Screen distance from `mouse` to a curve of another sketch (a polyline through it).
float CurveDistance(const SketchView& view, ImVec2 mouse, const std::vector<Vec2>& points) {
    float best = 1e9f;
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
        best = std::min(best, SegmentDistance(mouse, view.ToScreen(points[i]), view.ToScreen(points[i + 1])));
    return best;
}

std::vector<Vec2> ArcPoints(Vec2 c, double r, double a0, double sweep) {
    std::vector<Vec2> out;
    constexpr int steps = 48;
    for (int i = 0; i <= steps; ++i) {
        const double t = a0 + sweep * i / steps;
        out.push_back(c + Vec2{std::cos(t), std::sin(t)} * r);
    }
    return out;
}

/// Length of a projected line through work geometry: about the part's size.
double WorkHalfLength(const Session& session) {
    if (const auto b = session.model.BoundingBox()) return std::max((b->max - b->min).Length() * 0.6, 20.0);
    return 50.0;
}

const char* OriginLabel(ObjectId id) {
    namespace o = doc::origin;
    if (id == o::kPoint) return N_("Начало координат");
    if (id == o::kAxisX) return N_("Ось X");
    if (id == o::kAxisY) return N_("Ось Y");
    if (id == o::kAxisZ) return N_("Ось Z");
    if (id == o::kPlaneXY) return N_("Плоскость XY");
    if (id == o::kPlaneXZ) return N_("Плоскость XZ");
    if (id == o::kPlaneYZ) return N_("Плоскость YZ");
    return nullptr;
}

/// A work element (or origin element) that can be projected: placed, above the sketch.
bool ProjectableWork(const Session& session, ObjectId id, std::size_t index) {
    if (doc::origin::IsOriginElement(id)) return true;
    const int at = session.doc.IndexOf(id);
    if (at < 0 || static_cast<std::size_t>(at) >= index) return false;
    const doc::Feature* f = session.doc.Features()[static_cast<std::size_t>(at)].get();
    return dynamic_cast<const doc::WorkPlaneFeature*>(f) || dynamic_cast<const doc::WorkAxisFeature*>(f) ||
           dynamic_cast<const doc::WorkPointFeature*>(f);
}

/// The sheet metal parts («Лист») above feature `index`.
std::vector<ObjectId> SheetsAbove(const Session& session, std::size_t index) {
    std::vector<ObjectId> out;
    for (std::size_t i = 0; i < std::min(index, session.doc.Features().size()); ++i)
        if (const auto* s = dynamic_cast<const doc::SheetMetalFeature*>(session.doc.Features()[i].get()); s && !s->suppressed)
            out.push_back(s->id);
    return out;
}

} // namespace

// --- What else can be projected: work geometry, the origin, other sketches ---------

void SketchEditor::DrawProjectTargets(AppContext& app, Session& session, const SketchView& view) {
    myProjectHot = {};
    doc::SketchFeature* f = Active(session);
    if (!f || myOptions.projectMode != ProjectMode::Geometry) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    const auto before = static_cast<std::size_t>(index);
    const ViewportFrame& vf = view.vf;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::Ui& ui = app.ui;
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool hovered = vf.hovered;

    // A work element or the origin chosen in the tree meanwhile: projected at once.
    if (session.selectedFeature && session.selectedFeature != f->id && ProjectableWork(session, session.selectedFeature, before)) {
        const ObjectId work = session.selectedFeature;
        session.selectedFeature = f->id;
        auto old = f->Clone();
        Projector put(session.doc, *f, std::max(view.MmPerPixel(), 1e-6) * 1e-3, myOptions.projectLinked, myOptions.style != LineStyle::Normal);
        const Expected<kernel::ProjectedItem> item = kernel::ProjectWork(session.doc, work, f->placement.frame, WorkHalfLength(session));
        doc::Projection link;
        link.of = Of::Work;
        link.work = work;
        if (!item) {
            app.notes.Info(Tr(item.GetError().message));
        } else if (put.Item(*item, &link)) {
            session.doc.MarkChanged(before);
            session.CommitInPlaceEdit(std::move(old), Tr("Проецирование рабочей геометрии"));
        } else {
            app.notes.Info(Tr("Это уже спроецировано"));
        }
        return;
    }

    // Other visible sketches above this one, muted; the curve under the cursor stands out.
    const Color hot = ui.C().warning;
    float bestCurve = ui.S(6);
    for (const doc::SketchFeature* other : VisibleSketches(session, before, f->id)) {
        const SketchView ov{other->placement.frame, vf};
        SketchDrawStyle style;
        style.passive = true;
        DrawSketch(ui, dl, *other, ov, session.doc.units, style);
        if (!hovered) continue;
        const sketch::Sketch& s = other->sketch;
        const auto consider = [&](ObjectId id, float d, bool point) {
            // Points win over curves within reach (they sit on them).
            const float reach = point ? ui.S(7) : bestCurve;
            if (d >= reach || (myProjectHot.point && !point)) return;
            if (!point) bestCurve = d;
            myProjectHot = {ProjectTarget::Kind::Sketch, id, other->id, point, Trf("{} · {}", other->name, point ? Tr("точка") : Tr("кривая"))};
        };
        for (const sketch::Line& l : s.lines)
            consider(l.id, CurveDistance(ov, mouse, {s.PositionOf(l.start), s.PositionOf(l.end)}), false);
        for (const sketch::Circle& c : s.circles) consider(c.id, CurveDistance(ov, mouse, ArcPoints(s.PositionOf(c.center), c.radius, 0, 2 * kPi)), false);
        for (const sketch::Arc& a : s.arcs) {
            const Vec2 c = s.PositionOf(a.center), p0 = s.PositionOf(a.start), p1 = s.PositionOf(a.end);
            const double a0 = std::atan2(p0.y - c.y, p0.x - c.x);
            double sweep = std::atan2(p1.y - c.y, p1.x - c.x) - a0;
            while (sweep <= 1e-12) sweep += 2 * kPi;
            consider(a.id, CurveDistance(ov, mouse, ArcPoints(c, (p0 - c).Length(), a0, sweep)), false);
        }
        for (const sketch::Ellipse& e : s.ellipses) {
            const Vec2 c = s.PositionOf(e.center), u = s.PositionOf(e.major) - c, v = s.PositionOf(e.minor) - c;
            std::vector<Vec2> pts;
            for (int i = 0; i <= 64; ++i) pts.push_back(c + u * std::cos(2 * kPi * i / 64) + v * std::sin(2 * kPi * i / 64));
            consider(e.id, CurveDistance(ov, mouse, pts), false);
        }
        for (const sketch::Point& p : s.points) {
            if (p.isOrigin) continue;
            const ImVec2 at = ov.ToScreen(p.pos);
            consider(p.id, std::hypot(at.x - mouse.x, at.y - mouse.y), true);
        }
    }

    // Work planes (faint), axes and points, and the origin's point and axes.
    const ObjectId treeHot = app.shell.hoveredTreeItem;
    for (const ShownPlane& p : WorkPlanes(session, before)) DrawPlane(app, dl, vf, p, p.id == treeHot, p.id != treeHot);
    for (const ShownPlane& p : OriginPlanes(session))
        if (p.id == treeHot) DrawPlane(app, dl, vf, p, true);
    std::vector<ShownPoint> points = WorkPoints(session, before);
    points.push_back({doc::origin::kPoint, {0, 0, 0}, Tr("Начало координат")});
    const ShownPoint* hotPoint = hovered && !myProjectHot.point ? PointUnderCursor(app, vf, mouse, points) : nullptr;
    // The origin's axes: short dash-dot lines through it, coloured as in the corner triad.
    const double reach = WorkHalfLength(session) * 0.5;
    const std::array<std::tuple<ObjectId, Vec3, Color>, 3> originAxes{{{doc::origin::kAxisX, {1, 0, 0}, ui.C().axisX},
                                                                       {doc::origin::kAxisY, {0, 1, 0}, ui.C().axisY},
                                                                       {doc::origin::kAxisZ, {0, 0, 1}, ui.C().axisZ}}};
    ObjectId hotAxis;
    float axisBest = ui.S(5);
    for (const auto& [id, dir, colour] : originAxes) {
        const ImVec2 a = vf.Project(dir * -reach), b = vf.Project(dir * reach);
        const float d = SegmentDistance(mouse, a, b);
        if (hovered && d < axisBest) {
            axisBest = d;
            hotAxis = id;
        }
    }
    if (!hotAxis && hovered) hotAxis = AxisUnderCursor(app, vf, mouse, session, before);
    if (hotPoint) {
        myProjectHot = {ProjectTarget::Kind::Work, hotPoint->id, {}, true, hotPoint->label};
    } else if (!myProjectHot.point && myProjectHot.kind == ProjectTarget::Kind::None && hotAxis) {
        const char* label = OriginLabel(hotAxis);
        const doc::Feature* wf = session.doc.Find(hotAxis);
        myProjectHot = {ProjectTarget::Kind::Work, hotAxis, {}, false, label ? std::string(Tr(label)) : wf ? wf->name : std::string()};
    }
    for (const auto& [id, dir, colour] : originAxes) {
        const bool lit = id == hotAxis && myProjectHot.id == id;
        const ImVec2 a = vf.Project(dir * -reach), b = vf.Project(dir * reach);
        dl->AddLine(a, b, ui::ToU32(lit ? hot : colour.WithAlpha(0.45f)), ui.S(lit ? 2.2f : 1.0f));
    }
    DrawWorkAxes(app, dl, vf, session, before, myProjectHot.kind == ProjectTarget::Kind::Work ? myProjectHot.id : ObjectId{});
    DrawWorkPoints(app, dl, vf, points, myProjectHot.kind == ProjectTarget::Kind::Work ? myProjectHot.id : ObjectId{});

    // The sketch curve under the cursor, drawn over in the "what you get" colour.
    if (myProjectHot.kind == ProjectTarget::Kind::Sketch)
        if (const auto* other = session.doc.FindAs<doc::SketchFeature>(myProjectHot.sketch)) {
            const SketchView ov{other->placement.frame, vf};
            const sketch::Sketch& s = other->sketch;
            const float w = ui.S(2.4f);
            if (const sketch::Line* l = s.FindLine(myProjectHot.id)) DrawSketchSegment(ui, dl, ov, s.PositionOf(l->start), s.PositionOf(l->end), hot, w);
            if (const sketch::Circle* c = s.FindCircle(myProjectHot.id)) DrawSketchArc(ui, dl, ov, s.PositionOf(c->center), c->radius, 0, 2 * kPi, hot, w);
            if (const sketch::Arc* a = s.FindArc(myProjectHot.id)) {
                const Vec2 c = s.PositionOf(a->center), p0 = s.PositionOf(a->start), p1 = s.PositionOf(a->end);
                double a0 = std::atan2(p0.y - c.y, p0.x - c.x), a1 = std::atan2(p1.y - c.y, p1.x - c.x);
                while (a1 <= a0) a1 += 2 * kPi;
                DrawSketchArc(ui, dl, ov, c, (p0 - c).Length(), a0, a1, hot, w);
            }
            if (const sketch::Point* p = s.FindPoint(myProjectHot.id)) dl->AddCircleFilled(ov.ToScreen(p->pos), ui.S(4.5f), ui::ToU32(hot));
        }
    // What a click projects, by the cursor.
    if (hovered && myProjectHot.kind != ProjectTarget::Kind::None && !myProjectHot.label.empty()) {
        const std::string text = Trf("{} → {}", myProjectHot.label,
                                     myProjectHot.point ? Tr("точка")
                                     : myProjectHot.kind == ProjectTarget::Kind::Sketch ? Tr("кривая")
                                     : Tr("линия или точка"));
        ui::FontScope font(ui.fonts.caption);
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const ImVec2 at{mouse.x + ui.S(16), mouse.y + ui.S(14)};
        dl->AddRectFilled({at.x - ui.S(6), at.y - ui.S(3)}, {at.x + size.x + ui.S(6), at.y + size.y + ui.S(3)}, ui::ToU32(ui.C().tooltip),
                          ui.S(5));
        dl->AddText(at, ui::ToU32(ui.C().tooltipText), text.c_str());
    }
}

// --- The click -------------------------------------------------------------------

void SketchEditor::HandleProject(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    const int index = session.doc.IndexOf(f->id);
    if (index < 0 || !view.vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    const Vec2 pixel = view.vf.ToLocal(ImGui::GetMousePos());
    if (myOptions.projectMode == ProjectMode::Cut) {
        app.notes.Info(Tr("Сечение проецируется кнопкой «Спроецировать сечение» в панели"));
        return;
    }
    if (myOptions.projectMode == ProjectMode::Flat) {
        ProjectFlat(app, session, view, pixel);
        return;
    }
    const double tolerance = std::max(view.MmPerPixel(), 1e-6) * 1e-3;
    const bool construction = myOptions.style != LineStyle::Normal;
    const auto commit = [&](std::unique_ptr<doc::Feature> before, Projector& put, const char* label) {
        if (put.added == 0) {
            app.notes.Info(put.already > 0 ? Tr("Это уже спроецировано") : Tr("Здесь нечего проецировать"));
            return;
        }
        session.doc.MarkChanged(static_cast<std::size_t>(index));
        session.CommitInPlaceEdit(std::move(before), Tr(label));
    };

    // A point-like target first (a work point, the origin, a point of another
    // sketch), then a model vertex: they sit on edges and would be lost to them.
    const bool pointTarget = myProjectHot.kind != ProjectTarget::Kind::None && myProjectHot.point;
    const std::optional<render::Viewport::VertexPick> vertex = pointTarget ? std::nullopt : app.viewport.PickVertex(pixel);
    if (pointTarget || vertex) {
        auto before = f->Clone();
        Projector put(session.doc, *f, tolerance, myOptions.projectLinked, construction);
        doc::Projection link;
        Expected<kernel::ProjectedItem> item = Error{""};
        const char* label = "Проецирование точки";
        if (vertex) {
            if (!vertex->edge) {
                app.notes.Info(Tr("Вершина не найдена на модели"));
                return;
            }
            link.of = Of::Vertex;
            link.edge = *vertex->edge;
            link.at = vertex->point;
            kernel::ProjectedItem p;
            p.point = f->placement.frame.ToLocal(vertex->point);
            item = p;
            label = "Проецирование вершины";
        } else if (myProjectHot.kind == ProjectTarget::Kind::Work) {
            link.of = Of::Work;
            link.work = myProjectHot.id;
            item = kernel::ProjectWork(session.doc, link.work, f->placement.frame, WorkHalfLength(session));
            label = "Проецирование рабочей геометрии";
        } else {
            link.of = Of::Sketch;
            link.source = {myProjectHot.sketch, myProjectHot.id, 0.0, 0.0, 1.0};
            item = kernel::ProjectSketchEntity(session.doc, link.source, f->placement.frame);
            label = "Проецирование из другого эскиза";
        }
        if (!item) {
            app.notes.Info(Tr(item.GetError().message));
            return;
        }
        put.Item(*item, &link);
        commit(std::move(before), put, label);
        return;
    }

    // The viewport picks the bodies before this sketch (ViewportPanel turns
    // that on while this tool is active): an edge near the cursor, else the
    // whole outline of the face under it. A face of a body of revolution
    // gives the body's axis, outline and cut by the sketch plane instead.
    std::vector<doc::EdgeReference> edges;
    const std::optional<doc::EdgeReference> pickedEdge = app.viewport.PickEdge(pixel);
    if (pickedEdge) edges.push_back(*pickedEdge);

    // A curve of another sketch or a work axis under the cursor, when no model edge is.
    if (!pickedEdge && myProjectHot.kind != ProjectTarget::Kind::None) {
        auto before = f->Clone();
        Projector put(session.doc, *f, tolerance, myOptions.projectLinked, construction);
        doc::Projection link;
        Expected<kernel::ProjectedItem> item = Error{""};
        if (myProjectHot.kind == ProjectTarget::Kind::Work) {
            link.of = Of::Work;
            link.work = myProjectHot.id;
            item = kernel::ProjectWork(session.doc, link.work, f->placement.frame, WorkHalfLength(session));
        } else {
            link.of = Of::Sketch;
            link.source = {myProjectHot.sketch, myProjectHot.id, 0.0, 0.0, 1.0};
            item = kernel::ProjectSketchEntity(session.doc, link.source, f->placement.frame);
        }
        if (!item) {
            app.notes.Info(Tr(item.GetError().message));
            return;
        }
        put.Item(*item, &link);
        commit(std::move(before), put,
               myProjectHot.kind == ProjectTarget::Kind::Work ? "Проецирование рабочей геометрии" : "Проецирование из другого эскиза");
        return;
    }

    std::optional<kernel::RevolvedProjection> revolved;
    std::string revolvedProblem;
    const auto tryRevolved = [&] {
        const std::optional<doc::FaceReference> face = app.viewport.PickFace(pixel, true);
        if (!face || !face->curved) return;
        Expected<kernel::RevolvedProjection> r = session.model.ProjectRevolved(*face, f->placement.frame, static_cast<std::size_t>(index));
        if (r) revolved = std::move(*r);
        else revolvedProblem = r.GetError().message;
    };
    // A part edited in place: an edge of another part of the assembly (the
    // faded ones around it). Linked, it follows that part — adaptivity.
    // An edge of it, or — clicked on a face — the face's whole outline, as
    // for the part's own face.
    struct FromContext {
        kernel::ProjectedCurve  curve;
        doc::ContextProjection  link;
    };
    std::vector<FromContext> fromContext;
    const bool inAssembly = session.host && session.host->assembly;
    // Projects the picked edges of another part; false when the click ends
    // here (a note said why). A single edge that fails says so; of a face's
    // outline the edges that do not project (seen end-on) are left out.
    const auto projectContext = [&](const std::vector<render::Viewport::ContextEdge>& picked) {
        if (picked.empty()) return true;
        AssemblyState& st = *session.host->assembly;
        const ObjectId target = session.inPlaceOf;
        const std::string name = session.host->ContextName();
        if (kernel::WouldCycle(st.geometry, st.assembly, picked.front().body, target, name)) {
            app.notes.Info(Tr("Эта деталь сама следует за той, что правится: проекция замкнула бы их друг на друга"));
            return false;
        }
        std::string failure;
        for (const render::Viewport::ContextEdge& ce : picked) {
            const std::optional<doc::EdgeReference> source = st.geometry.SourceEdge(st.assembly, target, ce.body, ce.edge);
            if (!source) {
                failure = Tr("Ребро другой детали не найдено");
                continue;
            }
            doc::ContextProjection link{name, ce.body, *source, {}, true};
            const Expected<kernel::ProjectedCurve> pc = st.geometry.ProjectSourceEdge(st.assembly, target, link, f->placement.frame);
            if (!pc) {
                failure = Tr(pc.GetError().message);
                continue;
            }
            fromContext.push_back({*pc, std::move(link)});
        }
        if (fromContext.empty() && !failure.empty()) {
            app.notes.Info(failure);
            return false;
        }
        return true;
    };
    if (!pickedEdge && inAssembly) {
        if (const std::optional<render::Viewport::ContextEdge> ce = app.viewport.PickContextEdge(pixel))
            if (!projectContext({*ce})) return;
    }
    // An edge that does not project on its own (a circle of a turned part
    // seen from the side) is taken as a click on the body of revolution.
    if (pickedEdge && !session.model.ProjectEdge(*pickedEdge, f->placement.frame, static_cast<std::size_t>(index))) tryRevolved();
    if (!pickedEdge && fromContext.empty()) {
        tryRevolved();
        if (!revolved) edges = app.viewport.PickFaceEdges(pixel);
        if (!revolved && edges.empty() && inAssembly && !projectContext(app.viewport.PickContextFaceEdges(pixel))) return;
    }
    if (revolved) edges.clear();
    // Nothing of the model: a work plane under the cursor gives the line where it crosses the sketch.
    if (edges.empty() && !revolved && fromContext.empty() && revolvedProblem.empty()) {
        std::vector<ShownPlane> planes = WorkPlanes(session, static_cast<std::size_t>(index));
        if (const ShownPlane* plane = PlaneUnderCursor(view.vf, ImGui::GetMousePos(), planes)) {
            auto before = f->Clone();
            Projector put(session.doc, *f, tolerance, myOptions.projectLinked, construction);
            doc::Projection link;
            link.of = Of::Work;
            link.work = plane->id;
            const Expected<kernel::ProjectedItem> item = kernel::ProjectWork(session.doc, plane->id, f->placement.frame, WorkHalfLength(session));
            if (!item) {
                app.notes.Info(Tr(item.GetError().message));
                return;
            }
            put.Item(*item, &link);
            commit(std::move(before), put, "Проецирование рабочей геометрии");
            return;
        }
    }
    if (edges.empty() && !revolved && fromContext.empty()) {
        // A body of revolution that could not be projected says why (it
        // used to end here in silence: "the click did nothing").
        if (!revolvedProblem.empty()) app.notes.Info(Tr(revolvedProblem));
        return;
    }

    auto before = f->Clone();
    Projector put(session.doc, *f, tolerance, myOptions.projectLinked, construction);
    std::string problem;
    for (FromContext& c : fromContext) {
        if (!put.Curve(c.curve, nullptr)) continue;
        // Linked: it follows the other part (fixed for the solver, moved by
        // the assembly); independent: ordinary geometry held in place.
        if (myOptions.projectLinked) {
            c.link.curve = put.last;
            f->contextProjections.push_back(c.link);
        }
    }
    // The edges not projected yet, each with where it projects.
    std::vector<std::pair<doc::EdgeReference, kernel::ProjectedCurve>> fresh;
    for (const doc::EdgeReference& edge : edges) {
        bool known = false;
        for (const doc::Projection& p : f->projections)
            known = known || (p.of == Of::Edge && p.edge.curve == edge.curve && (p.edge.middle - edge.middle).Length() < 1e-6);
        if (known) {
            ++put.already;
            continue;
        }
        const Expected<kernel::ProjectedCurve> projected =
            session.model.ProjectEdge(edge, f->placement.frame, static_cast<std::size_t>(index));
        if (!projected) {
            problem = projected.GetError().message;
            continue;
        }
        fresh.emplace_back(edge, *projected);
    }
    // A face's outline whose loops cross or overlap on the sketch plane: cut
    // where they cross, what is covered twice kept once. The pieces are not
    // whole edges, so they cannot follow the model: they are held in place.
    bool trimmed = false;
    if (fresh.size() > 1) {
        std::vector<kernel::ProjectedCurve> curves;
        for (const auto& [e, c] : fresh) curves.push_back(c);
        const std::vector<kernel::ProjectedCurve> clean = kernel::TrimOverlappingCurves(curves, trimmed);
        if (trimmed) {
            Projector held(session.doc, *f, tolerance, false, construction);
            held.placed = put.placed;
            for (const kernel::ProjectedCurve& c : clean) held.Curve(c, nullptr);
            put.added += held.added;
            put.already += held.already;
            app.notes.Info(Tr("Петли контура грани перекрывались — контур подрезан и за моделью не следует"));
        }
    }
    if (!trimmed)
        for (const auto& [edge, curve] : fresh) {
            doc::Projection link;
            link.edge = edge;
            put.Curve(curve, &link);
        }
    if (revolved) {
        // Not linked to the model: an outline is not an edge to find again.
        if (revolved->axis) put.Curve(*revolved->axis, nullptr, true);
        if (revolved->centre) {
            const std::size_t count = f->sketch.points.size();
            const ObjectId c = put.Point(*revolved->centre);
            if (!myOptions.projectLinked)
                if (sketch::Point* pt = f->sketch.FindPoint(c)) pt->projected = false;
            if (f->sketch.points.size() > count) ++put.added;
        }
        for (const kernel::ProjectedCurve& c : revolved->outline) put.Curve(c, nullptr);
        for (const kernel::ProjectedCurve& c : revolved->section) put.Curve(c, nullptr);
        if (revolved->outlineSkipped)
            app.notes.Info(Tr("Ось тела вращения наклонена к плоскости эскиза: спроецированы только ось и сечение"));
    }
    if (put.added == 0) {
        if (problem.empty()) problem = revolvedProblem;
        app.notes.Info(!problem.empty() ? Tr(problem) : put.already > 0 ? Tr("Это уже спроецировано") : Tr("Здесь нечего проецировать"));
        return;
    }
    if (!problem.empty()) app.notes.Info(Trf("Часть рёбер не спроецирована: {}", Tr(problem)));
    session.doc.MarkChanged(static_cast<std::size_t>(index));
    session.CommitInPlaceEdit(std::move(before), !fromContext.empty() ? Tr("Проецирование из другой детали")
                                                 : revolved            ? Tr("Проецирование тела вращения")
                                                 : edges.size() > 1    ? Tr("Проецирование грани")
                                                                       : Tr("Проецирование геометрии"));
}

// --- Сечение и развёртка -------------------------------------------------------------

void SketchEditor::ProjectCut(AppContext& app, Session& session) {
    doc::SketchFeature* f = Active(session);
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    const std::vector<kernel::ProjectedCurve> cut = session.model.ProjectSection(f->placement.frame, static_cast<std::size_t>(index));
    if (cut.empty()) {
        app.notes.Info(Tr("Плоскость эскиза не пересекает деталь"));
        return;
    }
    auto before = f->Clone();
    // Always an independent copy, as Inventor's Project Cut Edges.
    Projector put(session.doc, *f, 1e-6, false, myOptions.style != LineStyle::Normal);
    for (const kernel::ProjectedCurve& c : cut) put.Curve(c, nullptr);
    if (put.added == 0) {
        app.notes.Info(Tr("Сечение уже спроецировано"));
        return;
    }
    session.doc.MarkChanged(static_cast<std::size_t>(index));
    session.CommitInPlaceEdit(std::move(before), Tr("Проецирование сечения"));
}

void SketchEditor::ProjectFlat(AppContext& app, Session& session, const SketchView& view, Vec2 pixel) {
    doc::SketchFeature* f = Active(session);
    const int index = session.doc.IndexOf(f->id);
    const std::vector<ObjectId> sheets = SheetsAbove(session, static_cast<std::size_t>(index));
    if (sheets.empty()) {
        app.notes.Info(Tr("Нет листового тела выше эскиза"));
        return;
    }
    // Which sheet: the body under the cursor (one sheet needs no aiming).
    ObjectId base = sheets.size() == 1 ? sheets.front() : ObjectId{};
    if (!base) {
        if (const auto face = app.viewport.PickFace(pixel, true)) {
            const ObjectId body = session.model.BodyNearest(face->point);
            for (const ObjectId s : sheets)
                if (s == body) base = s;
        }
        if (!base) {
            app.notes.Info(Tr("Щёлкните листовую деталь"));
            return;
        }
    }
    const std::optional<kernel::SheetInfo> sheet = session.model.SheetBefore(session.doc, base, static_cast<std::size_t>(index));
    if (!sheet) {
        app.notes.Info(Tr("Листовая деталь не построена"));
        return;
    }
    const std::vector<kernel::PlanarCurve> curves = kernel::FlatPatternCurves(*sheet);
    const std::size_t bends = kernel::FlatPatternBends(*sheet);
    auto before = f->Clone();
    Projector put(session.doc, *f, std::max(view.MmPerPixel(), 1e-6) * 1e-3, myOptions.projectLinked, myOptions.style != LineStyle::Normal);
    std::string problem;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const bool bend = i + bends >= curves.size();
        if (bend && !myOptions.projectBends) continue;
        const Expected<kernel::ProjectedItem> item = kernel::ProjectPlanar(sheet->frame, curves[i], f->placement.frame);
        if (!item || !item->curve) {
            if (!item) problem = item.GetError().message;
            continue;
        }
        doc::Projection link;
        link.of = Of::Flat;
        link.work = base;
        link.part = static_cast<int>(i);
        put.Curve(*item->curve, &link, bend);   // bend lines as осевые
    }
    if (put.added == 0) {
        app.notes.Info(!problem.empty() ? Tr(problem) : put.already > 0 ? Tr("Развёртка уже спроецирована") : Tr("Здесь нечего проецировать"));
        return;
    }
    if (!problem.empty()) app.notes.Info(Trf("Часть развёртки не спроецирована: {}", Tr(problem)));
    session.doc.MarkChanged(static_cast<std::size_t>(index));
    session.CommitInPlaceEdit(std::move(before), Tr("Проецирование развёртки"));
}

// --- Автопроецирование ---------------------------------------------------------------

bool SketchEditor::AutoProject(AppContext& app, Session& session, const SketchView& view, Vec2 pixel, Snap& snap) {
    doc::SketchFeature* f = Active(session);
    if (!f) return false;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return false;
    const double tolerance = std::max(view.MmPerPixel(), 1e-6) * 1e-3;
    // The edit goes into the step being drawn: no undo step of its own.
    Projector put(session.doc, *f, tolerance, true, false);
    if (const auto v = app.viewport.PickVertex(pixel); v && v->edge) {
        doc::Projection link;
        link.of = Of::Vertex;
        link.edge = *v->edge;
        link.at = v->point;
        const Vec2 at = f->placement.frame.ToLocal(v->point);
        if (!put.LonePoint(at, &link)) {
            // There already: snap to it.
            for (const sketch::Point& p : f->sketch.points)
                if ((p.isOrigin || p.projected) && (p.pos - at).Length() < tolerance) put.last = p.id;
        }
        if (!put.last) return false;
        snap = {};
        snap.kind = SnapKind::Point;
        snap.point = put.last;
        snap.pos = at;
        return true;
    }
    const std::optional<doc::EdgeReference> edge = app.viewport.PickEdge(pixel);
    if (!edge) return false;
    // The edge projected already: snap onto that curve.
    ObjectId curve;
    for (const doc::Projection& p : f->projections)
        if (p.of == Of::Edge && p.edge.curve == edge->curve && (p.edge.middle - edge->middle).Length() < 1e-6) curve = p.curve;
    if (!curve) {
        const Expected<kernel::ProjectedCurve> projected = session.model.ProjectEdge(*edge, f->placement.frame, static_cast<std::size_t>(index));
        if (!projected) return false;
        doc::Projection link;
        link.edge = *edge;
        if (!put.Curve(*projected, &link)) return false;
        curve = put.last;
    }
    // On the curve, where the cursor is (the nearest point of a line; on a circle or arc, round the centre).
    const std::optional<Vec2> cursor = view.FromScreen(view.vf.ToScreen(pixel));
    if (!cursor) return false;
    Vec2 at = *cursor;
    const sketch::Sketch& sk = f->sketch;
    if (const sketch::Line* l = sk.FindLine(curve)) {
        const Vec2 a = sk.PositionOf(l->start), b = sk.PositionOf(l->end), d = b - a;
        const double t = std::clamp((at - a).Dot(d) / std::max(d.Dot(d), 1e-12), 0.0, 1.0);
        at = a + d * t;
    } else if (const sketch::Circle* c = sk.FindCircle(curve)) {
        const Vec2 o = sk.PositionOf(c->center);
        at = o + (at - o).Normalized() * c->radius;
    } else if (const sketch::Arc* a = sk.FindArc(curve)) {
        const Vec2 o = sk.PositionOf(a->center);
        at = o + (at - o).Normalized() * (sk.PositionOf(a->start) - o).Length();
    }
    snap = {};
    snap.kind = SnapKind::Curve;
    snap.curve = curve;
    snap.pos = at;
    return true;
}

void SketchEditor::ProjectPartOrigin(Session& session, doc::SketchFeature& sketch) { app::ProjectPartOrigin(session.doc, sketch); }

} // namespace bz::app
