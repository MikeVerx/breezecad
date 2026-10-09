// Drawing, dimension and constraint tools of the sketch editor.

#include "app/Format.h"
#include "app/sketch/DimensionLayout.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "core/Expression.h"
#include "sketch/Shapes.h"
#include "sketch/Trim.h"
#include "sketch/Solver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace bz::app {
namespace {

using sketch::ConstraintType;
using sketch::EntityKind;

sketch::Constraint Make(ConstraintType type, ObjectId a, ObjectId b = {}) {
    sketch::Constraint c;
    c.type = type;
    c.refs = {a, b};
    return c;
}

bool IsRound(EntityKind k) { return k == EntityKind::Circle || k == EntityKind::Arc; }

double Sign(double v) { return v < 0 ? -1.0 : 1.0; }

/// Unit vector from `a` towards `b` (along X when they coincide).
Vec2 Direction(Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    return d.Length() > 1e-12 ? d.Normalized() : Vec2{1, 0};
}

/// A number typed into the tool panel; nullopt when empty, not a plain
/// value (names are resolved later, by the dimension) or not positive.
std::optional<double> TypedValue(const std::string& text, Quantity quantity, const UnitSystem& units) {
    if (text.empty()) return std::nullopt;
    const Expected<double> v = EvaluateExpression(text, quantity, units, [](std::string_view) { return std::optional<NamedValue>{}; });
    if (!v || (quantity != Quantity::Angle && *v <= 0)) return std::nullopt;
    return *v;
}

/// A slot as the clicks so far (the last one may be the cursor) describe it.
struct SlotPlan {
    bool   arc = false;
    Vec2   c1, c2;       ///< straight: the end centres
    Vec2   o, s, e;      ///< arc: centre, start and end centres (counter-clockwise)
    bool   swapped = false;   ///< arc: start and end clicks were swapped
    double width = 0;
};

bool ArcContains(Vec2 centre, Vec2 start, Vec2 end, Vec2 p);

/// Nullopt until the clicks fix the axis and the width (typed `width` > 0 wins).
std::optional<SlotPlan> PlanSlot(RectangleMode mode, const std::vector<Vec2>& pts, double typedWidth) {
    SlotPlan plan;
    plan.arc = IsArcSlot(mode);
    const std::size_t axisClicks = plan.arc ? 3 : 2;
    if (pts.size() < axisClicks + 1 && typedWidth <= 0) return std::nullopt;
    if (pts.size() < axisClicks) return std::nullopt;
    if (plan.arc) {
        if (mode == RectangleMode::SlotArc3) {
            const auto through = sketch::CircleThroughPoints(pts[0], pts[1], pts[2]);
            if (!through) return std::nullopt;
            plan.o = through->centre;
            plan.s = pts[0];
            plan.e = pts[1];
            if (!ArcContains(plan.o, plan.s, plan.e, pts[2])) {
                std::swap(plan.s, plan.e);
                plan.swapped = true;
            }
        } else {
            plan.o = pts[0];
            plan.s = pts[1];
            const double r = (pts[1] - pts[0]).Length();
            if (r < 1e-9 || (pts[2] - pts[0]).Length() < 1e-9) return std::nullopt;
            plan.e = pts[0] + (pts[2] - pts[0]).Normalized() * r;
            if ((plan.s - plan.o).Cross(plan.e - plan.o) < 0) {
                std::swap(plan.s, plan.e);
                plan.swapped = true;
            }
        }
        const double radius = (plan.s - plan.o).Length();
        plan.width = typedWidth > 0 ? typedWidth : pts.size() > 3 ? 2 * std::abs((pts[3] - plan.o).Length() - radius) : 0;
        if (plan.width <= 1e-9 || plan.width >= 2 * radius) return std::nullopt;
        return plan;
    }
    Vec2 a = pts[0], b = pts[1];
    if (mode == RectangleMode::SlotCentre) a = pts[0] * 2.0 - pts[1];
    if ((b - a).Length() < 1e-9) return std::nullopt;
    const Vec2 d = (b - a).Normalized();
    plan.width = typedWidth > 0 ? typedWidth : pts.size() > 2 ? 2 * std::abs((pts[2] - a).Cross(d)) : 0;
    if (plan.width <= 1e-9) return std::nullopt;
    if (mode == RectangleMode::SlotOverall) {   // the clicks are the very ends
        if ((b - a).Length() <= plan.width) return std::nullopt;
        a = a + d * (plan.width / 2);
        b = b - d * (plan.width / 2);
    }
    plan.c1 = a;
    plan.c2 = b;
    return plan;
}

/// Whether the counter-clockwise arc from `start` to `end` around `centre` passes `p`.
bool ArcContains(Vec2 centre, Vec2 start, Vec2 end, Vec2 p) {
    const auto angle = [&](Vec2 q) { return std::atan2(q.y - centre.y, q.x - centre.x); };
    const auto norm = [](double a) { while (a < 0) a += 2 * kPi; while (a >= 2 * kPi) a -= 2 * kPi; return a; };
    const double a0 = angle(start);
    return norm(angle(p) - a0) <= norm(angle(end) - a0);
}
bool IsCurve(EntityKind k) { return k == EntityKind::Line || IsRound(k); }

/// The constraint a tool makes from picked entities; nullopt when the picks
/// do not fit the tool (yet). `complete` says whether more picks could help.
std::optional<sketch::Constraint> ConstraintFromPicks(SketchTool tool, const sketch::Sketch& sk,
                                                      const std::vector<ObjectId>& picks, bool& needMore) {
    needMore = false;
    const EntityKind k0 = picks.size() > 0 ? sk.KindOf(picks[0]) : EntityKind::None;
    const EntityKind k1 = picks.size() > 1 ? sk.KindOf(picks[1]) : EntityKind::None;
    const ObjectId a = picks.size() > 0 ? picks[0] : ObjectId{};
    const ObjectId b = picks.size() > 1 ? picks[1] : ObjectId{};

    switch (tool) {
        case SketchTool::Horizontal:
        case SketchTool::Vertical: {
            const ConstraintType t = tool == SketchTool::Horizontal ? ConstraintType::Horizontal : ConstraintType::Vertical;
            if (k0 == EntityKind::Line) return Make(t, a);
            if (k0 == EntityKind::Point && picks.size() == 1) { needMore = true; return std::nullopt; }
            if (k0 == EntityKind::Point && k1 == EntityKind::Point) return Make(t, a, b);
            return std::nullopt;
        }
        case SketchTool::Fix:
            if (k0 == EntityKind::Point) {
                sketch::Constraint c = Make(ConstraintType::Fix, a);
                c.fixedAt = sk.PositionOf(a);
                return c;
            }
            return std::nullopt;
        case SketchTool::Coincident:
            if (picks.size() < 2) { needMore = true; return std::nullopt; }
            if (k0 == EntityKind::Point && k1 == EntityKind::Point) return Make(ConstraintType::Coincident, a, b);
            if (k0 == EntityKind::Point && IsCurve(k1)) return Make(ConstraintType::PointOnCurve, a, b);
            if (IsCurve(k0) && k1 == EntityKind::Point) return Make(ConstraintType::PointOnCurve, b, a);
            return std::nullopt;
        case SketchTool::Parallel:
        case SketchTool::Perpendicular:
            if (picks.size() < 2) { needMore = k0 == EntityKind::Line; return std::nullopt; }
            if (k0 == EntityKind::Line && k1 == EntityKind::Line)
                return Make(tool == SketchTool::Parallel ? ConstraintType::Parallel : ConstraintType::Perpendicular, a, b);
            return std::nullopt;
        case SketchTool::Equal:
            if (picks.size() < 2) { needMore = IsCurve(k0); return std::nullopt; }
            if ((k0 == EntityKind::Line && k1 == EntityKind::Line) || (IsRound(k0) && IsRound(k1)))
                return Make(ConstraintType::Equal, a, b);
            return std::nullopt;
        case SketchTool::Tangent:
            if (picks.size() < 2) { needMore = IsCurve(k0); return std::nullopt; }
            if ((IsCurve(k0) && IsRound(k1)) || (IsRound(k0) && IsCurve(k1))) return Make(ConstraintType::Tangent, a, b);
            return std::nullopt;
        case SketchTool::Midpoint:
            if (picks.size() < 2) { needMore = k0 == EntityKind::Point || k0 == EntityKind::Line; return std::nullopt; }
            if (k0 == EntityKind::Point && k1 == EntityKind::Line) return Make(ConstraintType::Midpoint, a, b);
            if (k0 == EntityKind::Line && k1 == EntityKind::Point) return Make(ConstraintType::Midpoint, b, a);
            return std::nullopt;
        case SketchTool::Concentric:
            if (picks.size() < 2) { needMore = IsRound(k0); return std::nullopt; }
            if (IsRound(k0) && IsRound(k1)) return Make(ConstraintType::Concentric, a, b);
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

/// Dimension a set of picks and a cursor position describe (nullopt: not yet).
std::optional<sketch::Constraint> DimensionFromPicks(const sketch::Sketch& sk, const std::vector<ObjectId>& picks,
                                                     Vec2 cursor) {
    if (picks.empty()) return std::nullopt;
    const EntityKind k0 = sk.KindOf(picks[0]);
    const EntityKind k1 = picks.size() > 1 ? sk.KindOf(picks[1]) : EntityKind::None;
    sketch::Constraint c;

    if (picks.size() == 1) {
        if (k0 == EntityKind::Line) c = Make(ConstraintType::Length, picks[0]);
        else if (k0 == EntityKind::Circle) c = Make(ConstraintType::Diameter, picks[0]);
        else if (k0 == EntityKind::Arc) c = Make(ConstraintType::Radius, picks[0]);
        else return std::nullopt;
    } else if (k0 == EntityKind::Point && k1 == EntityKind::Point) {
        // Horizontal, vertical or aligned, depending on where the label goes.
        const Vec2 p = sk.PositionOf(picks[0]), q = sk.PositionOf(picks[1]);
        const bool betweenX = cursor.x > std::min(p.x, q.x) && cursor.x < std::max(p.x, q.x);
        const bool betweenY = cursor.y > std::min(p.y, q.y) && cursor.y < std::max(p.y, q.y);
        const ConstraintType t = betweenX && !betweenY ? ConstraintType::DistanceX
                               : betweenY && !betweenX ? ConstraintType::DistanceY
                                                       : ConstraintType::Distance;
        c = Make(t, picks[0], picks[1]);
    } else if (k0 == EntityKind::Line && k1 == EntityKind::Line) {
        const sketch::Line* l1 = sk.FindLine(picks[0]);
        const sketch::Line* l2 = sk.FindLine(picks[1]);
        const Vec2 d1 = sk.PositionOf(l1->end) - sk.PositionOf(l1->start);
        const Vec2 d2 = sk.PositionOf(l2->end) - sk.PositionOf(l2->start);
        const bool parallel = std::abs(d1.Normalized().Cross(d2.Normalized())) < 1e-6;
        c = parallel ? Make(ConstraintType::PointLineDistance, l2->start, picks[0]) : Make(ConstraintType::Angle, picks[0], picks[1]);
    } else if (k0 == EntityKind::Point && k1 == EntityKind::Line) {
        c = Make(ConstraintType::PointLineDistance, picks[0], picks[1]);
    } else if (k0 == EntityKind::Line && k1 == EntityKind::Point) {
        c = Make(ConstraintType::PointLineDistance, picks[1], picks[0]);
    } else {
        return std::nullopt;
    }
    if (const auto anchor = DimensionAnchor(sk, c)) c.labelOffset = cursor - *anchor;
    c.parameter.quantity = c.type == ConstraintType::Angle ? Quantity::Angle : Quantity::Length;
    return c;
}

} // namespace

SketchEditor::PointRef SketchEditor::RefFromSnap(const Snap& snap) const {
    return {snap.kind == SnapKind::Point ? snap.point : ObjectId{}, snap.pos,
            snap.kind == SnapKind::Curve ? snap.curve : ObjectId{}, snap.kind == SnapKind::Midpoint ? snap.curve : ObjectId{}};
}

ObjectId SketchEditor::Resolve(doc::Document& doc, sketch::Sketch& sk, const PointRef& ref) {
    if (ref.id && sk.FindPoint(ref.id)) return ref.id;
    const ObjectId id = sk.AddPoint(doc.Ids(), ref.pos);
    if (ref.onCurve && sk.KindOf(ref.onCurve) != EntityKind::None)
        sk.AddConstraint(doc.Ids(), Make(ConstraintType::PointOnCurve, id, ref.onCurve));
    if (ref.midOf && sk.KindOf(ref.midOf) == EntityKind::Line)
        sk.AddConstraint(doc.Ids(), Make(ConstraintType::Midpoint, id, ref.midOf));
    return id;
}

void SketchEditor::HandleDrawing(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    SnapOptions options;
    options.points = app.settings.pointSnap;
    options.grid = app.settings.gridSnap;
    options.gridStep = app.settings.gridStep > 0 ? app.settings.gridStep : AdaptiveGridStep(view.MmPerPixel()) / 2;
    if (!myClicks.empty() && (myTool == SketchTool::Line)) options.from = myClicks.back().pos;
    mySnap = SnapCursor(f->sketch, view, ImGui::GetMousePos(), options);
    // A typed size wins over snapping: the point lands where the size says.
    if (ApplyTypedSizes(session.doc.units, mySnap.pos)) {
        const Vec2 pos = mySnap.pos;
        mySnap = Snap{};
        mySnap.pos = pos;
    }
    myCursor = mySnap.pos;
    myMmPerPixel = view.MmPerPixel();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && view.vf.hovered) {
        myClicks.push_back(RefFromSnap(mySnap));
        myLastClickTime = ImGui::GetIO().MouseClickedTime[ImGuiMouseButton_Left];
        FinishDrawingStep(app, session, mySnap);
    }
}

void SketchEditor::DropPressClick() {
    // Only a click made by this very press (ImGui keeps the press time).
    if (!myClicks.empty() && myLastClickTime == ImGui::GetIO().MouseClickedTime[ImGuiMouseButton_Left]) myClicks.pop_back();
}

void SketchEditor::HandleProject(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    const int index = session.doc.IndexOf(f->id);
    if (index < 0 || !view.vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    // The viewport picks edges of the bodies before this sketch (ViewportPanel
    // turns that on while this tool is active).
    const std::optional<doc::EdgeReference> edge = app.viewport.PickEdge(view.vf.ToLocal(ImGui::GetMousePos()));
    if (!edge) return;
    for (const doc::Projection& p : f->projections)
        if (p.edge.curve == edge->curve && (p.edge.middle - edge->middle).Length() < 1e-6) {
            app.notes.Info("Это ребро уже спроецировано");
            return;
        }
    const Expected<kernel::ProjectedCurve> projected =
        session.model.ProjectEdge(*edge, f->placement.frame, static_cast<std::size_t>(index));
    if (!projected) {
        app.notes.Info(projected.GetError().message);
        return;
    }

    auto before = f->Clone();
    sketch::Sketch& sk = f->sketch;
    IdGenerator& ids = session.doc.Ids();
    const auto point = [&](Vec2 at) {
        const ObjectId id = sk.AddPoint(ids, at);
        sk.FindPoint(id)->projected = true;
        return id;
    };
    ObjectId curve;
    switch (projected->kind) {
        case kernel::ProjectedCurve::Kind::Line:
            curve = sk.AddLine(ids, point(projected->start), point(projected->end));
            sk.FindLine(curve)->projected = true;
            break;
        case kernel::ProjectedCurve::Kind::Circle:
            curve = sk.AddCircle(ids, point(projected->centre), projected->radius);
            sk.FindCircle(curve)->projected = true;
            break;
        case kernel::ProjectedCurve::Kind::Arc:
            curve = sk.AddArc(ids, point(projected->centre), point(projected->start), point(projected->end));
            for (auto& a : sk.arcs)
                if (a.id == curve) a.projected = true;
            break;
    }
    f->projections.push_back({*edge, curve});
    session.doc.MarkChanged(static_cast<std::size_t>(index));
    session.CommitInPlaceEdit(std::move(before), "Проецирование геометрии");
}

std::size_t SketchEditor::ClicksNeeded() const {
    switch (myTool) {
        case SketchTool::Point:     return 1;
        case SketchTool::Arc:       return 3;
        case SketchTool::Rectangle:
            switch (myOptions.rectangle) {
                case RectangleMode::ThreePoints:
                case RectangleMode::ThreePointsCentre:
                case RectangleMode::SlotCentres:
                case RectangleMode::SlotOverall:
                case RectangleMode::SlotCentre:    return 3;
                case RectangleMode::SlotArc3:
                case RectangleMode::SlotArcCentre: return 4;
                default:                           return 2;
            }
        case SketchTool::Circle:    return myOptions.circle == CircleMode::ThreePoints ? 3 : 2;
        default:                    return 2;
    }
}

bool SketchEditor::ApplyTypedSizes(const UnitSystem& units, Vec2& pos) const {
    if (myClicks.empty()) return false;
    const Vec2 first = myClicks.front().pos, last = myClicks.back().pos;
    const std::size_t step = myClicks.size();
    const auto length = [&](const std::string& text) { return TypedValue(text, Quantity::Length, units); };

    switch (myTool) {
        case SketchTool::Line: {
            const auto l = length(myOptions.length);
            const auto a = TypedValue(myOptions.angle, Quantity::Angle, units);
            if (!l && !a) return false;
            const Vec2 d = pos - last;
            const Vec2 dir = a ? Vec2{std::cos(*a), std::sin(*a)} : Direction(last, pos);
            pos = last + dir * (l ? *l : d.Dot(dir));
            return true;
        }
        case SketchTool::Rectangle: {
            const auto w = length(myOptions.width), h = length(myOptions.height);
            if (myOptions.rectangle == RectangleMode::Polygon) {
                const auto size = length(myOptions.size);
                if (step != 1 || !size) return false;
                pos = first + Direction(first, pos) * (*size / 2);
                return true;
            }
            if (IsSlot(myOptions.rectangle) || myOptions.rectangle == RectangleMode::ThreePointsCentre) return false;
            if (myOptions.rectangle == RectangleMode::ThreePoints) {
                if (step == 1 && w) {
                    pos = first + Direction(first, pos) * *w;
                    return true;
                }
                if (step == 2 && h) {
                    const Vec2 side = last - first;
                    const Vec2 normal = Vec2{-side.y, side.x}.Normalized();
                    pos = last + normal * (*h * Sign((pos - last).Dot(normal)));
                    return true;
                }
                return false;
            }
            if (step != 1 || (!w && !h)) return false;
            const double factor = myOptions.rectangle == RectangleMode::Centre ? 0.5 : 1.0;
            if (w) pos.x = first.x + Sign(pos.x - first.x) * *w * factor;
            if (h) pos.y = first.y + Sign(pos.y - first.y) * *h * factor;
            return true;
        }
        case SketchTool::Circle:
        case SketchTool::Arc: {
            const bool sized = myTool == SketchTool::Circle ? myOptions.circle == CircleMode::CentreRadius
                                                            : myOptions.arc == ArcMode::CentreEnds;
            const auto size = length(myOptions.size);
            if (step != 1 || !sized || !size) return false;
            const double r = myTool == SketchTool::Circle ? *size / 2 : *size;   // diameter / radius
            pos = first + Direction(first, pos) * r;
            return true;
        }
        default: return false;
    }
}

void SketchEditor::AddTypedDimensions(doc::Document& doc, sketch::Sketch& sk, const std::vector<TypedDimension>& dims) {
    for (const auto& [constraint, text, inside] : dims) {
        sketch::Constraint c = constraint;
        if (text.empty()) continue;
        c.parameter.name = doc.NextParameterName();
        c.parameter.expression = text;
        c.parameter.quantity = Quantity::Length;
        // Label a little off the curve, in screen terms.
        const double off = 28.0 * myMmPerPixel;
        if (const sketch::Line* l = sk.FindLine(c.refs[0]); l && c.type == ConstraintType::Length) {
            const Vec2 a = sk.PositionOf(l->start), d = sk.PositionOf(l->end) - a;
            Vec2 normal = Vec2{-d.y, d.x}.Normalized();
            if (inside && (*inside - a).Dot(normal) > 0) normal = normal * -1.0;
            c.labelOffset = normal * off;
        } else if (c.type == ConstraintType::Diameter || c.type == ConstraintType::Radius) {
            c.labelOffset = Vec2{1, 1}.Normalized() * (sk.RadiusOf(c.refs[0]) + off);
        }
        sk.AddConstraint(doc.Ids(), std::move(c));
        // Each one gets a fresh name: NextParameterName looks at the sketch.
        doc.EvaluateParameters();
    }
}

void SketchEditor::HandleTrim(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    const bool onCurve = myHover && IsCurve(myHover.kind);
    if (!onCurve || !myCursor) return;
    // Projected geometry belongs to the model.
    const sketch::Sketch& sk = f->sketch;
    const sketch::Line* line = sk.FindLine(myHover.id);
    const sketch::Circle* circle = sk.FindCircle(myHover.id);
    const sketch::Arc* arc = sk.FindArc(myHover.id);
    if ((line && line->projected) || (circle && circle->projected) || (arc && arc->projected)) return;
    const std::optional<sketch::TrimSpan> span = sketch::FindTrimSpan(f->sketch, myHover.id, *myCursor);
    if (!span) return;

    // The piece that would go: thick and dashed in the warning colour.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::vector<Vec2> pts = sketch::SpanPolyline(f->sketch, *span);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        DrawSketchSegment(app.ui, dl, view, pts[i], pts[i + 1], app.ui.C().danger, 3.0f, true);

    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    auto before = f->Clone();
    sketch::Sketch trial = f->sketch;
    if (!sketch::Trim(trial, session.doc.Ids(), *span)) return;
    if (!sketch::Solve(trial).converged) {
        app.notes.Error("После обрезки эскиз не решается — кусок оставлен");
        return;
    }
    f->sketch = std::move(trial);
    f->PruneProjections();
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), "Обрезка");
}

std::optional<sketch::Motion> SketchEditor::PendingMotion(const UnitSystem& units, bool withCursor) const {
    std::vector<Vec2> pts;
    for (const PointRef& c : myClicks) pts.push_back(c.pos);
    if (withCursor && myCursor) pts.push_back(mySnap.pos);
    if (myTool == SketchTool::Move) {
        if (pts.size() < 2) return std::nullopt;
        return sketch::Motion{{}, 0, pts[1] - pts[0]};
    }
    if (pts.empty()) return std::nullopt;
    // A typed angle needs only the centre; otherwise centre, from, to.
    if (const auto typed = TypedValue(myOptions.turn, Quantity::Angle, units)) return sketch::Motion{pts[0], *typed, {}};
    if (pts.size() < 3) return std::nullopt;
    const Vec2 from = pts[1] - pts[0], to = pts[2] - pts[0];
    if (from.Length() < 1e-9 || to.Length() < 1e-9) return std::nullopt;
    return sketch::Motion{pts[0], std::atan2(from.Cross(to), from.Dot(to)), {}};
}

void SketchEditor::ApplyMotion(AppContext& app, Session& session, const sketch::Motion& motion) {
    doc::SketchFeature* f = Active(session);
    if (!f || mySelection.empty()) return;
    const std::vector<ObjectId> chosen(mySelection.begin(), mySelection.end());
    const std::vector<ObjectId> moved = sketch::MovedPoints(f->sketch, chosen);
    if (moved.empty()) {
        app.notes.Info("Среди выбранного нет того, что можно сдвинуть (начало координат и проекции стоят на месте)");
        return;
    }
    const Vec2 expected = motion.Apply(f->sketch.PositionOf(moved.front()));
    sketch::Sketch trial = f->sketch;
    sketch::MoveEntities(trial, session.doc.Ids(), chosen, motion);
    if (!sketch::Solve(trial).converged) {
        app.notes.Error("Выбранное нельзя так сдвинуть: эскиз не решается");
        return;
    }
    const Box2 extents = f->sketch.Extents();
    const double scale = extents.IsEmpty() ? 1.0 : std::max((extents.max - extents.min).Length(), 1.0);
    if ((trial.PositionOf(moved.front()) - expected).Length() > 1e-4 * scale)
        app.notes.Info("Положение выбранного задано размерами или связями — сдвиг ограничен ими");
    auto before = f->Clone();
    f->sketch = std::move(trial);
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), InfoOf(myTool).label);
    myClicks.clear();
    myOptions.moveX.clear();
    myOptions.moveY.clear();
    myOptions.turn.clear();
}

void SketchEditor::HandleMotion(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    SnapOptions options;
    options.points = app.settings.pointSnap;
    options.grid = app.settings.gridSnap;
    options.gridStep = app.settings.gridStep > 0 ? app.settings.gridStep : AdaptiveGridStep(view.MmPerPixel()) / 2;
    mySnap = SnapCursor(f->sketch, view, ImGui::GetMousePos(), options);
    myCursor = mySnap.pos;

    // The selection follows the cursor.
    if (const auto motion = PendingMotion(session.doc.units, true); motion && !mySelection.empty()) {
        ui::Ui& ui = app.ui;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const sketch::Sketch& sk = f->sketch;
        const Color ink = ui.C().sketchHover;
        for (ObjectId id : mySelection) {
            if (const sketch::Line* l = sk.FindLine(id)) {
                DrawSketchSegment(ui, dl, view, motion->Apply(sk.PositionOf(l->start)), motion->Apply(sk.PositionOf(l->end)), ink, 2.0f);
            } else if (const sketch::Circle* c = sk.FindCircle(id)) {
                DrawSketchArc(ui, dl, view, motion->Apply(sk.PositionOf(c->center)), c->radius, 0, 2 * kPi, ink, 2.0f);
            } else if (const sketch::Arc* a = sk.FindArc(id)) {
                const Vec2 centre = motion->Apply(sk.PositionOf(a->center));
                const Vec2 p = motion->Apply(sk.PositionOf(a->start)) - centre, q = motion->Apply(sk.PositionOf(a->end)) - centre;
                double a0 = std::atan2(p.y, p.x), a1 = std::atan2(q.y, q.x);
                if (a1 <= a0) a1 += 2 * kPi;
                DrawSketchArc(ui, dl, view, centre, p.Length(), a0, a1, ink, 2.0f);
            }
        }
        if (!myClicks.empty()) DrawSketchSegment(ui, dl, view, myClicks.front().pos, mySnap.pos, ink.WithAlpha(0.6f), 1.2f, true);
    }
    DrawSnapMarker(app.ui, ImGui::GetWindowDrawList(), view.ToScreen(mySnap.pos), mySnap);

    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !view.vf.hovered) return;
    // Ctrl / Shift + click, or any click while nothing is chosen, picks elements.
    const ImGuiIO& io = ImGui::GetIO();
    if (myClicks.empty() && myHover && myHover.kind != EntityKind::Constraint && (io.KeyCtrl || io.KeyShift || mySelection.empty())) {
        if (!mySelection.erase(myHover.id)) mySelection.insert(myHover.id);
        return;
    }
    if (mySelection.empty()) return;
    myClicks.push_back(RefFromSnap(mySnap));
    const std::size_t need = myTool == SketchTool::Move ? 2 : TypedValue(myOptions.turn, Quantity::Angle, session.doc.units) ? 1 : 3;
    if (myClicks.size() < need) return;
    myClicks.pop_back();
    if (const auto motion = PendingMotion(session.doc.units, true)) ApplyMotion(app, session, *motion);
    myClicks.clear();
}

void SketchEditor::FinishDrawingStep(AppContext& app, Session& session, const Snap& snap) {
    doc::SketchFeature* f = Active(session);
    doc::Document& d = session.doc;
    const std::size_t need = ClicksNeeded();
    if (myClicks.size() < need) {
        // A rectangle's first side or a circle's chord must not be empty.
        if (myClicks.size() >= 2 && (myClicks.back().pos - myClicks[myClicks.size() - 2].pos).Length() < 1e-9) myClicks.pop_back();
        return;
    }

    // Ignore a second click on the same spot (a double click, a zero-length line).
    if (need >= 2 && (myClicks.back().pos - myClicks[myClicks.size() - 2].pos).Length() < 1e-9) {
        myClicks.pop_back();
        return;
    }

    auto before = f->Clone();
    sketch::Sketch& sk = f->sketch;
    IdGenerator& ids = d.Ids();
    const char* label = InfoOf(myTool).label;
    const std::size_t linesBefore = sk.lines.size(), circlesBefore = sk.circles.size(), arcsBefore = sk.arcs.size();
    std::optional<PointRef> continueFrom;
    std::vector<TypedDimension> dims;
    const auto refuse = [&](const char* why) {
        f->sketch = static_cast<const doc::SketchFeature&>(*before).sketch;
        myClicks.pop_back();
        app.notes.Info(why);
    };

    switch (myTool) {
        case SketchTool::Point:
            Resolve(d, sk, myClicks[0]);
            break;
        case SketchTool::Line: {
            const ObjectId a = Resolve(d, sk, myClicks[0]);
            const ObjectId b = Resolve(d, sk, myClicks[1]);
            const ObjectId line = sk.AddLine(ids, a, b);
            // Inferred alignment becomes a constraint, unless already implied.
            if (snap.horizontal || snap.vertical) {
                const sketch::Constraint c = Make(snap.horizontal ? ConstraintType::Horizontal : ConstraintType::Vertical, line);
                if (!sketch::IsRedundant(sk, c)) sk.AddConstraint(ids, c);
            }
            dims.push_back({Make(ConstraintType::Length, line), myOptions.length, {}});
            continueFrom = PointRef{b, sk.PositionOf(b), {}, {}};
            break;
        }
        case SketchTool::Rectangle: {
            const RectangleMode mode = myOptions.rectangle;
            if (mode == RectangleMode::Polygon) {
                const sketch::PolygonIds p = sketch::AddPolygon(sk, ids, Resolve(d, sk, myClicks[0]), myClicks[1].pos,
                                                                myOptions.polygonSides, myOptions.polygonInscribed);
                dims.push_back({Make(ConstraintType::Diameter, p.circle), myOptions.size, {}});
                label = "Многоугольник";
                break;
            }
            if (IsSlot(mode)) {
                std::vector<Vec2> pts;
                for (const PointRef& c : myClicks) pts.push_back(c.pos);
                const auto plan = PlanSlot(mode, pts, TypedValue(myOptions.width, Quantity::Length, d.units).value_or(0.0));
                if (!plan) return refuse("Паз не получится: проверьте точки и ширину");
                sketch::SlotIds slot;
                if (plan->arc) {
                    const bool byCentre = mode == RectangleMode::SlotArcCentre;
                    const ObjectId o = byCentre ? Resolve(d, sk, myClicks[0]) : sk.AddPoint(ids, plan->o);
                    PointRef startRef = myClicks[byCentre ? 1 : 0], endRef = myClicks[byCentre ? 2 : 1];
                    if (plan->swapped) std::swap(startRef, endRef);
                    startRef.pos = plan->s;
                    endRef.pos = plan->e;
                    if (byCentre && !plan->swapped) endRef.id = {};   // the end is put onto the radius
                    slot = sketch::AddArcSlot(sk, ids, o, Resolve(d, sk, startRef), Resolve(d, sk, endRef), plan->width);
                } else {
                    ObjectId c1, c2;
                    if (mode == RectangleMode::SlotCentres) {
                        c1 = Resolve(d, sk, myClicks[0]);
                        c2 = Resolve(d, sk, myClicks[1]);
                    } else {
                        c1 = sk.AddPoint(ids, plan->c1);
                        c2 = sk.AddPoint(ids, plan->c2);
                    }
                    slot = sketch::AddStraightSlot(sk, ids, c1, c2, plan->width);
                    if (mode == RectangleMode::SlotCentre)
                        sk.AddConstraint(ids, Make(ConstraintType::Midpoint, Resolve(d, sk, myClicks[0]), slot.axis));
                }
                dims.push_back({Make(ConstraintType::Diameter, slot.cap), myOptions.width, {}});
                label = "Паз";
                break;
            }
            std::optional<sketch::RectangleIds> r;
            if (mode == RectangleMode::ThreePoints) {
                r = sketch::AddThreePointRectangle(sk, ids, Resolve(d, sk, myClicks[0]), Resolve(d, sk, myClicks[1]), myClicks[2].pos);
                if (!r) return refuse("Третья точка лежит на первой стороне — прямоугольник не получится");
            } else if (mode == RectangleMode::ThreePointsCentre) {
                r = sketch::AddThreePointCentredRectangle(sk, ids, Resolve(d, sk, myClicks[0]), myClicks[1].pos, myClicks[2].pos);
                if (!r) return refuse("Точки на одной прямой — прямоугольник не получится");
            } else {
                const Vec2 p = myClicks[0].pos, q = myClicks[1].pos;
                if (std::abs(p.x - q.x) < 1e-9 || std::abs(p.y - q.y) < 1e-9) {
                    myClicks.pop_back();
                    return;
                }
                r = mode == RectangleMode::Centre
                        ? sketch::AddCentredRectangle(sk, ids, Resolve(d, sk, myClicks[0]), q)
                        : sketch::AddCornerRectangle(sk, ids, Resolve(d, sk, myClicks[0]), Resolve(d, sk, myClicks[1]));
            }
            const Vec2 centre = (sk.PositionOf(r->corners[0]) + sk.PositionOf(r->corners[2])) * 0.5;
            dims.push_back({Make(ConstraintType::Length, r->sides[0]), myOptions.width, centre});
            dims.push_back({Make(ConstraintType::Length, r->sides[1]), myOptions.height, centre});
            break;
        }
        case SketchTool::Circle: {
            if (myOptions.circle == CircleMode::ThreePoints) {
                const auto through = sketch::CircleThroughPoints(myClicks[0].pos, myClicks[1].pos, myClicks[2].pos);
                if (!through) return refuse("Три точки на одной прямой — окружность через них не проходит");
                const ObjectId circle = sk.AddCircle(ids, sk.AddPoint(ids, through->centre), through->radius);
                for (const PointRef& click : myClicks)
                    if (click.id) sk.AddConstraint(ids, Make(ConstraintType::PointOnCurve, click.id, circle));
                break;
            }
            const ObjectId centre = Resolve(d, sk, myClicks[0]);
            const double r = (myClicks[1].pos - myClicks[0].pos).Length();
            const ObjectId circle = sk.AddCircle(ids, centre, r);
            if (myClicks[1].id) sk.AddConstraint(ids, Make(ConstraintType::PointOnCurve, myClicks[1].id, circle));
            dims.push_back({Make(ConstraintType::Diameter, circle), myOptions.size, {}});
            break;
        }
        case SketchTool::Arc: {
            Vec2 c;
            PointRef startRef, endRef;
            if (myOptions.arc == ArcMode::ThreePoints) {
                // Start, end, then a point the arc passes through.
                const auto through = sketch::CircleThroughPoints(myClicks[0].pos, myClicks[1].pos, myClicks[2].pos);
                if (!through) return refuse("Три точки на одной прямой — дуга через них не проходит");
                c = through->centre;
                startRef = myClicks[0];
                endRef = myClicks[1];
                if (!ArcContains(c, startRef.pos, endRef.pos, myClicks[2].pos)) std::swap(startRef, endRef);
            } else {
                c = myClicks[0].pos;
                const double r = (myClicks[1].pos - c).Length();
                startRef = myClicks[1];
                endRef = myClicks[2];
                if (!endRef.id) endRef.pos = c + (endRef.pos - c).Normalized() * r;
                // The arc goes counter-clockwise; swap ends when drawn clockwise.
                if ((startRef.pos - c).Cross(endRef.pos - c) < 0) std::swap(startRef, endRef);
            }
            const ObjectId centre = myOptions.arc == ArcMode::ThreePoints ? sk.AddPoint(ids, c) : Resolve(d, sk, myClicks[0]);
            const ObjectId s = Resolve(d, sk, startRef);
            const ObjectId e = Resolve(d, sk, endRef);
            const ObjectId arc = sk.AddArc(ids, centre, s, e);
            if (myOptions.arc == ArcMode::CentreEnds) dims.push_back({Make(ConstraintType::Radius, arc), myOptions.size, {}});
            break;
        }
        default:
            break;
    }

    // The chosen line type goes to what was drawn (helper lines of a shape,
    // such as a centred rectangle's diagonal, stay helpers).
    if (myOptions.style != LineStyle::Normal) {
        const bool centre = myOptions.style == LineStyle::Centerline;
        for (std::size_t i = linesBefore; i < sk.lines.size(); ++i)
            if (!sk.lines[i].construction) (centre ? sk.lines[i].centerline : sk.lines[i].construction) = true;
        for (std::size_t i = circlesBefore; i < sk.circles.size(); ++i) sk.circles[i].construction = true;
        for (std::size_t i = arcsBefore; i < sk.arcs.size(); ++i) sk.arcs[i].construction = true;
    }
    AddTypedDimensions(d, sk, dims);
    if (!sketch::Solve(sk).converged) {
        f->sketch = static_cast<const doc::SketchFeature&>(*before).sketch;
        d.EvaluateParameters();
        myClicks.clear();
        app.notes.Error("Введённые размеры не сходятся с другими ограничениями эскиза");
        return;
    }
    d.MarkChanged(static_cast<std::size_t>(d.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), label);
    // Typed sizes are for one shape, as in KOMPAS.
    myOptions.length.clear();
    myOptions.angle.clear();
    myOptions.width.clear();
    myOptions.height.clear();
    myOptions.size.clear();
    myClicks.clear();
    if (continueFrom) myClicks.push_back(*continueFrom);   // lines go on in a chain
}


bool SketchEditor::AddConstraint(AppContext& app, Session& session, sketch::Constraint c, const char* label) {
    doc::SketchFeature* f = Active(session);
    if (!f) return false;
    if (sketch::IsRedundant(f->sketch, c)) {
        if (!sketch::IsDimensional(c.type)) {
            app.notes.Info("Это ограничение уже следует из других — оно не нужно");
            return false;
        }
        c.reference = true;   // a driven dimension only shows the value
        app.notes.Info("Размер уже определён другими — добавлен как справочный");
    }
    sketch::Sketch trial = f->sketch;
    const ObjectId id = trial.AddConstraint(session.doc.Ids(), c);
    if (!sketch::Solve(trial).converged) {
        app.notes.Error("Ограничение противоречит существующим");
        return false;
    }
    auto before = f->Clone();
    f->sketch = std::move(trial);
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), label);
    if (sketch::IsDimensional(c.type) && !c.reference)
        if (const sketch::Constraint* added = Active(session)->sketch.FindConstraint(id)) StartInlineEdit(*added);
    return true;
}

void SketchEditor::TryApplyToSelection(AppContext& app, Session& session) {
    doc::SketchFeature* f = Active(session);
    if (!f || mySelection.empty()) return;
    std::vector<ObjectId> picks(mySelection.begin(), mySelection.end());
    // Points first: tools expect "point, curve" order where it matters.
    std::sort(picks.begin(), picks.end(), [&](ObjectId x, ObjectId y) {
        return (f->sketch.KindOf(x) == EntityKind::Point) > (f->sketch.KindOf(y) == EntityKind::Point);
    });
    bool needMore = false;
    if (auto c = ConstraintFromPicks(myTool, f->sketch, picks, needMore)) {
        if (AddConstraint(app, session, *c, InfoOf(myTool).label)) {
            mySelection.clear();
            myTool = SketchTool::Select;
        }
    }
}

void SketchEditor::HandleConstraint(AppContext& app, Session& session, const SketchView& view) {
    (void)view;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (!myHover || myHover.kind == EntityKind::Constraint) return;
    doc::SketchFeature* f = Active(session);
    // A second click on a picked element drops it: a wrong first pick is
    // replaced without leaving the tool.
    if (const auto it = std::find(myPicks.begin(), myPicks.end(), myHover.id); it != myPicks.end()) {
        myPicks.erase(it);
        return;
    }
    myPicks.push_back(myHover.id);

    bool needMore = false;
    const auto c = ConstraintFromPicks(myTool, f->sketch, myPicks, needMore);
    if (c) {
        AddConstraint(app, session, *c, InfoOf(myTool).label);
        myPicks.clear();
    } else if (!needMore) {
        app.notes.Info(std::string("Не подходит для «") + InfoOf(myTool).label + "». " + InfoOf(myTool).hint);
        myPicks.clear();
    }
}

void SketchEditor::HandleDimension(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !myCursor) return;

    if (myHover && myHover.kind == EntityKind::Constraint && myPicks.empty()) {
        if (const sketch::Constraint* c = f->sketch.FindConstraint(myHover.id); c && sketch::IsDimensional(c->type))
            StartInlineEdit(*c);
        return;
    }
    if (myHover && myHover.kind != EntityKind::Constraint && myPicks.size() < 2 &&
        std::find(myPicks.begin(), myPicks.end(), myHover.id) == myPicks.end()) {
        myPicks.push_back(myHover.id);
        return;
    }
    // A click on empty space places the dimension.
    auto c = DimensionFromPicks(f->sketch, myPicks, *myCursor);
    myPicks.clear();
    if (!c) return;
    const std::optional<double> measured = MeasureDimension(f->sketch, *c);
    if (!measured) return;
    const double value = c->type == ConstraintType::Angle ? std::abs(*measured) : *measured;
    c->parameter.name = session.doc.NextParameterName();
    c->parameter.value = c->type == ConstraintType::Angle ? *measured : value;
    c->parameter.expression = FormatValue(value, c->parameter.quantity, session.doc.units, 3);
    if (c->type == ConstraintType::Angle && *measured < 0) c->parameter.expression = "-" + c->parameter.expression;
    AddConstraint(app, session, *c, "Размер");
    (void)view;
}

void SketchEditor::DrawShapePreview(AppContext& app, const SketchView& view, const UnitSystem& units) const {
    ui::Ui& ui = app.ui;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color ink = ui.C().sketchHover;
    const Color faint = ink.WithAlpha(0.6f);
    const Vec2 a = myClicks.front().pos, b = mySnap.pos;
    const std::size_t step = myClicks.size();
    const auto segment = [&](Vec2 p, Vec2 q) { DrawSketchSegment(ui, dl, view, p, q, ink, 2.0f); };
    const auto loop = [&](std::span<const Vec2> pts) {
        for (std::size_t i = 0; i < pts.size(); ++i) segment(pts[i], pts[(i + 1) % pts.size()]);
    };
    const auto circle = [&](Vec2 c, double r, bool construction) {
        DrawSketchArc(ui, dl, view, c, r, 0, 2 * kPi, construction ? faint : ink, construction ? 1.2f : 2.0f, construction);
    };
    const auto arc = [&](Vec2 c, Vec2 from, Vec2 to) {
        double a0 = std::atan2(from.y - c.y, from.x - c.x), a1 = std::atan2(to.y - c.y, to.x - c.x);
        if (a1 <= a0) a1 += 2 * kPi;
        DrawSketchArc(ui, dl, view, c, (from - c).Length(), a0, a1, ink, 2.0f);
    };

    switch (myTool) {
        case SketchTool::Line: segment(myClicks.back().pos, b); break;
        case SketchTool::Rectangle: {
            const RectangleMode mode = myOptions.rectangle;
            if (mode == RectangleMode::Polygon) {
                const double distance = (b - a).Length();
                const int n = std::max(myOptions.polygonSides, 3);
                const double stepAngle = 2 * kPi / n;
                const double radius = myOptions.polygonInscribed ? distance : distance / std::cos(stepAngle / 2);
                const double start = std::atan2(b.y - a.y, b.x - a.x) + (myOptions.polygonInscribed ? 0.0 : stepAngle / 2);
                std::vector<Vec2> pts;
                for (int i = 0; i < n; ++i)
                    pts.push_back(a + Vec2{std::cos(start + i * stepAngle), std::sin(start + i * stepAngle)} * radius);
                circle(a, distance, true);
                loop(pts);
            } else if (IsSlot(mode)) {
                std::vector<Vec2> pts;
                for (const PointRef& c : myClicks) pts.push_back(c.pos);
                pts.push_back(b);
                const double typed = TypedValue(myOptions.width, Quantity::Length, units).value_or(0.0);
                if (const auto plan = PlanSlot(mode, pts, typed); plan && pts.size() > (plan->arc ? 3u : 2u)) {
                    const std::vector<Vec2> outline = plan->arc ? sketch::ArcSlotOutline(plan->o, plan->s, plan->e, plan->width)
                                                                : sketch::StraightSlotOutline(plan->c1, plan->c2, plan->width);
                    loop(outline);
                } else if (IsArcSlot(mode) && pts.size() == 3) {
                    if (mode == RectangleMode::SlotArc3) {
                        if (const auto c = sketch::CircleThroughPoints(pts[0], pts[1], pts[2])) {
                            if (ArcContains(c->centre, pts[0], pts[1], pts[2])) arc(c->centre, pts[0], pts[1]);
                            else arc(c->centre, pts[1], pts[0]);
                        }
                    } else {
                        const Vec2 e = pts[0] + Direction(pts[0], pts[2]) * (pts[1] - pts[0]).Length();
                        if ((pts[1] - pts[0]).Cross(e - pts[0]) < 0) arc(pts[0], e, pts[1]);
                        else arc(pts[0], pts[1], e);
                    }
                } else {
                    const Vec2 from = mode == RectangleMode::SlotCentre && pts.size() == 2 ? pts[0] * 2.0 - b : pts[pts.size() - 2];
                    DrawSketchSegment(ui, dl, view, from, b, faint, 1.2f, LinePattern::DashDot);
                }
            } else if (mode == RectangleMode::ThreePoints || mode == RectangleMode::ThreePointsCentre) {
                if (step == 1) {
                    segment(mode == RectangleMode::ThreePointsCentre ? a * 2.0 - b : a, b);
                } else {
                    const Vec2 p = myClicks[1].pos;
                    if (mode == RectangleMode::ThreePoints) {
                        const Vec2 side = p - a;
                        const Vec2 normal = Vec2{-side.y, side.x}.Normalized();
                        const Vec2 off = normal * (b - p).Dot(normal);
                        const std::array<Vec2, 4> pts{a, p, p + off, a + off};
                        loop(pts);
                    } else {
                        const Vec2 u = p - a;
                        const Vec2 n = Vec2{-u.y, u.x}.Normalized();
                        const Vec2 v = n * (b - a).Dot(n);
                        const std::array<Vec2, 4> pts{a - u - v, a + u - v, a + u + v, a - u + v};
                        loop(pts);
                    }
                }
            } else {
                const Vec2 p = mode == RectangleMode::Centre ? a * 2.0 - b : a;
                const std::array<Vec2, 4> pts{p, Vec2{b.x, p.y}, b, Vec2{p.x, b.y}};
                loop(pts);
                if (mode == RectangleMode::Centre) DrawSketchSegment(ui, dl, view, p, b, faint, 1.2f, true);
            }
            break;
        }
        case SketchTool::Circle:
            if (myOptions.circle == CircleMode::ThreePoints) {
                if (step == 1) segment(a, b);
                else if (const auto c = sketch::CircleThroughPoints(a, myClicks[1].pos, b)) circle(c->centre, c->radius, false);
            } else {
                circle(a, (b - a).Length(), false);
            }
            break;
        case SketchTool::Arc:
            if (myOptions.arc == ArcMode::ThreePoints) {
                if (step == 1) {
                    segment(a, b);
                } else if (const auto c = sketch::CircleThroughPoints(a, myClicks[1].pos, b)) {
                    if (ArcContains(c->centre, a, myClicks[1].pos, b)) arc(c->centre, a, myClicks[1].pos);
                    else arc(c->centre, myClicks[1].pos, a);
                }
            } else if (step == 1) {
                DrawSketchSegment(ui, dl, view, a, b, faint, 1.2f);
            } else {
                const Vec2 s = myClicks[1].pos;
                const Vec2 e = a + Direction(a, b) * (s - a).Length();
                if ((s - a).Cross(e - a) < 0) arc(a, e, s);
                else arc(a, s, e);
            }
            break;
        default: break;
    }
}

void SketchEditor::DrawPreview(AppContext& app, const SketchView& view, const sketch::Sketch& sk,
                               const UnitSystem& units) {
    ui::Ui& ui = app.ui;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool drawing = myTool >= SketchTool::Line && myTool <= SketchTool::Point;
    if (drawing && myCursor) {
        DrawSnapGuides(ui, dl, view, mySnap);
        DrawSnapMarker(ui, dl, view.ToScreen(mySnap.pos), mySnap);
        if (!myClicks.empty()) DrawShapePreview(app, view, units);
    }

    if (myTool == SketchTool::Dimension && myCursor && !myPicks.empty() && !myHover) {
        if (const auto c = DimensionFromPicks(sk, myPicks, *myCursor)) {
            sketch::Constraint preview = *c;
            preview.reference = true;   // shows the measured value
            if (const auto layout = LayoutDimension(sk, preview, units)) {
                DimensionLayout shown = *layout;
                if (!shown.text.empty() && shown.text.front() == '(') shown.text = shown.text.substr(1, shown.text.size() - 2);
                DrawDimension(ui, dl, view, shown, ui.C().sketchHover, true);
            }
        }
    }
}

} // namespace bz::app
