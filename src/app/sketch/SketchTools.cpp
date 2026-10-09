// Drawing, dimension and constraint tools of the sketch editor.

#include "app/Format.h"
#include "app/sketch/DimensionLayout.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "sketch/Solver.h"

#include <algorithm>
#include <cmath>

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
            snap.kind == SnapKind::Curve ? snap.curve : ObjectId{}};
}

ObjectId SketchEditor::Resolve(doc::Document& doc, sketch::Sketch& sk, const PointRef& ref) {
    if (ref.id && sk.FindPoint(ref.id)) return ref.id;
    const ObjectId id = sk.AddPoint(doc.Ids(), ref.pos);
    if (ref.onCurve && sk.KindOf(ref.onCurve) != EntityKind::None)
        sk.AddConstraint(doc.Ids(), Make(ConstraintType::PointOnCurve, id, ref.onCurve));
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
    myCursor = mySnap.pos;

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

void SketchEditor::FinishDrawingStep(AppContext& app, Session& session, const Snap& snap) {
    doc::SketchFeature* f = Active(session);
    doc::Document& d = session.doc;
    const std::size_t need = myTool == SketchTool::Point ? 1 : myTool == SketchTool::Arc ? 3 : 2;
    if (myClicks.size() < need) return;

    // Ignore a second click on the same spot (a double click, a zero-length line).
    if (need >= 2 && (myClicks.back().pos - myClicks[myClicks.size() - 2].pos).Length() < 1e-9) {
        myClicks.pop_back();
        return;
    }

    auto before = f->Clone();
    sketch::Sketch& sk = f->sketch;
    const char* label = InfoOf(myTool).label;
    std::optional<PointRef> continueFrom;

    switch (myTool) {
        case SketchTool::Point:
            Resolve(d, sk, myClicks[0]);
            break;
        case SketchTool::Line: {
            const ObjectId a = Resolve(d, sk, myClicks[0]);
            const ObjectId b = Resolve(d, sk, myClicks[1]);
            const ObjectId line = sk.AddLine(d.Ids(), a, b);
            // Inferred alignment becomes a constraint, unless already implied.
            if (snap.horizontal || snap.vertical) {
                const sketch::Constraint c = Make(snap.horizontal ? ConstraintType::Horizontal : ConstraintType::Vertical, line);
                if (!sketch::IsRedundant(sk, c)) sk.AddConstraint(d.Ids(), c);
            }
            continueFrom = PointRef{b, sk.PositionOf(b), {}};
            break;
        }
        case SketchTool::Rectangle: {
            const Vec2 p = myClicks[0].pos, q = myClicks[1].pos;
            if (std::abs(p.x - q.x) < 1e-9 || std::abs(p.y - q.y) < 1e-9) {
                myClicks.pop_back();
                return;
            }
            const ObjectId c0 = Resolve(d, sk, myClicks[0]);
            const ObjectId c2 = Resolve(d, sk, myClicks[1]);
            const ObjectId c1 = sk.AddPoint(d.Ids(), {q.x, p.y});
            const ObjectId c3 = sk.AddPoint(d.Ids(), {p.x, q.y});
            const ObjectId l0 = sk.AddLine(d.Ids(), c0, c1), l1 = sk.AddLine(d.Ids(), c1, c2);
            const ObjectId l2 = sk.AddLine(d.Ids(), c2, c3), l3 = sk.AddLine(d.Ids(), c3, c0);
            for (const auto& [type, line] : {std::pair{ConstraintType::Horizontal, l0}, {ConstraintType::Vertical, l1},
                                             {ConstraintType::Horizontal, l2}, {ConstraintType::Vertical, l3}})
                if (!sketch::IsRedundant(sk, Make(type, line))) sk.AddConstraint(d.Ids(), Make(type, line));
            break;
        }
        case SketchTool::Circle: {
            const ObjectId center = Resolve(d, sk, myClicks[0]);
            const double r = (myClicks[1].pos - myClicks[0].pos).Length();
            const ObjectId circle = sk.AddCircle(d.Ids(), center, r);
            if (myClicks[1].id) sk.AddConstraint(d.Ids(), Make(ConstraintType::PointOnCurve, myClicks[1].id, circle));
            break;
        }
        case SketchTool::Arc: {
            const Vec2 c = myClicks[0].pos;
            const double r = (myClicks[1].pos - c).Length();
            PointRef endRef = myClicks[2];
            if (!endRef.id) endRef.pos = c + (endRef.pos - c).Normalized() * r;
            // The arc goes counter-clockwise; swap ends when drawn clockwise.
            PointRef startRef = myClicks[1];
            if ((startRef.pos - c).Cross(endRef.pos - c) < 0) std::swap(startRef, endRef);
            const ObjectId center = Resolve(d, sk, myClicks[0]);
            const ObjectId s = Resolve(d, sk, startRef);
            const ObjectId e = Resolve(d, sk, endRef);
            sk.AddArc(d.Ids(), center, s, e);
            break;
        }
        default:
            break;
    }

    sketch::Solve(sk);
    d.MarkChanged(static_cast<std::size_t>(d.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), label);
    myClicks.clear();
    if (continueFrom) myClicks.push_back(*continueFrom);   // lines go on in a chain
    (void)app;
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
    if (std::find(myPicks.begin(), myPicks.end(), myHover.id) != myPicks.end()) return;
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

void SketchEditor::DrawPreview(AppContext& app, const SketchView& view, const sketch::Sketch& sk,
                               const UnitSystem& units) {
    ui::Ui& ui = app.ui;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color ink = ui.C().sketchHover;

    const bool drawing = myTool >= SketchTool::Line && myTool <= SketchTool::Point;
    if (drawing && myCursor) {
        DrawSnapMarker(ui, dl, view.ToScreen(mySnap.pos), mySnap);
        if (!myClicks.empty()) {
            const Vec2 a = myClicks.front().pos, b = mySnap.pos;
            switch (myTool) {
                case SketchTool::Line: DrawSketchSegment(ui, dl, view, myClicks.back().pos, b, ink, 2.0f); break;
                case SketchTool::Rectangle:
                    DrawSketchSegment(ui, dl, view, a, {b.x, a.y}, ink, 2.0f);
                    DrawSketchSegment(ui, dl, view, {b.x, a.y}, b, ink, 2.0f);
                    DrawSketchSegment(ui, dl, view, b, {a.x, b.y}, ink, 2.0f);
                    DrawSketchSegment(ui, dl, view, {a.x, b.y}, a, ink, 2.0f);
                    break;
                case SketchTool::Circle: DrawSketchArc(ui, dl, view, a, (b - a).Length(), 0, 2 * kPi, ink, 2.0f); break;
                case SketchTool::Arc:
                    if (myClicks.size() == 1) {
                        DrawSketchSegment(ui, dl, view, a, b, ink.WithAlpha(0.6f), 1.2f);
                    } else {
                        const Vec2 s = myClicks[1].pos - a, e = b - a;
                        double a0 = std::atan2(s.y, s.x), a1 = std::atan2(e.y, e.x);
                        if (s.Cross(e) < 0) std::swap(a0, a1);
                        if (a1 <= a0) a1 += 2 * kPi;
                        DrawSketchArc(ui, dl, view, a, s.Length(), a0, a1, ink, 2.0f);
                    }
                    break;
                default: break;
            }
        }
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
