#include "app/sketch/SketchEditor.h"

#include "app/Format.h"
#include "app/sketch/DimensionLayout.h"
#include "app/sketch/SketchOverlay.h"
#include "document/Commands.h"
#include "sketch/Solver.h"
#include "ui/Widgets.h"

#include <imgui_stdlib.h>

#include <array>
#include <cmath>

namespace bz::app {
namespace {

constexpr std::array<SketchToolInfo, 17> kTools{{
    {SketchTool::Select, "Выбор", "ui-check", "Выберите или перетащите элементы эскиза"},
    {SketchTool::Line, "Отрезок", "sk-line", "Укажите начальную точку отрезка"},
    {SketchTool::Rectangle, "Прямоугольник", "sk-rectangle", "Укажите первый угол прямоугольника"},
    {SketchTool::Circle, "Окружность", "sk-circle", "Укажите центр окружности"},
    {SketchTool::Arc, "Дуга", "sk-arc", "Укажите центр дуги"},
    {SketchTool::Point, "Точка", "sk-point", "Укажите положение точки"},
    {SketchTool::Dimension, "Размер", "sk-dimension", "Выберите отрезок, окружность или две точки"},
    {SketchTool::Coincident, "Совпадение", "c-coincident", "Выберите две точки или точку и кривую"},
    {SketchTool::Horizontal, "Горизонтальность", "c-horizontal", "Выберите отрезок или две точки"},
    {SketchTool::Vertical, "Вертикальность", "c-vertical", "Выберите отрезок или две точки"},
    {SketchTool::Parallel, "Параллельность", "c-parallel", "Выберите два отрезка"},
    {SketchTool::Perpendicular, "Перпендикулярность", "c-perpendicular", "Выберите два отрезка"},
    {SketchTool::Equal, "Равенство", "c-equal", "Выберите два отрезка или две окружности"},
    {SketchTool::Tangent, "Касательность", "c-tangent", "Выберите кривую и окружность или дугу"},
    {SketchTool::Fix, "Фиксация", "c-fix", "Выберите точку, которую нужно закрепить"},
    {SketchTool::Midpoint, "Середина", "c-midpoint", "Выберите точку и отрезок"},
    {SketchTool::Concentric, "Концентричность", "c-concentric", "Выберите две окружности или дуги"},
}};

/// Sketch geometry plus dimension labels, in sketch coordinates.
Box2 SketchExtentsWithLabels(const doc::SketchFeature& f, const UnitSystem& units) {
    Box2 extents = f.sketch.Extents();
    for (const sketch::Constraint& c : f.sketch.constraints)
        if (sketch::IsDimensional(c.type))
            if (const auto layout = LayoutDimension(f.sketch, c, units)) extents.Include(layout->label);
    return extents;
}

/// The sketch's extents as a box in model space, for fitting the camera.
Box3 SketchBox(const doc::SketchFeature& f, const UnitSystem& units) {
    Box3 box;
    const Box2 extents = SketchExtentsWithLabels(f, units);
    if (extents.IsEmpty()) return box;
    const bz::Frame& frame = f.placement.frame;
    for (Vec2 corner : {extents.min, extents.max, Vec2{extents.min.x, extents.max.y}, Vec2{extents.max.x, extents.min.y}})
        box.Include(frame.ToWorld(corner));
    return box;
}

/// True when `c` is the only driving dimension of the sketch: the moment
/// Inventor-style auto-scaling applies.
bool IsFirstDimension(const sketch::Sketch& sk, const sketch::Constraint& c) {
    for (const sketch::Constraint& other : sk.constraints) {
        if (other.id == c.id) continue;
        if (other.type == sketch::ConstraintType::Fix) return false;   // something is pinned in place
        if (sketch::IsDimensional(other.type) && !other.reference) return false;
    }
    return c.type != sketch::ConstraintType::Angle;
}

} // namespace

std::span<const SketchToolInfo> SketchToolList() { return kTools; }

const SketchToolInfo& InfoOf(SketchTool tool) {
    for (const auto& t : kTools)
        if (t.tool == tool) return t;
    return kTools[0];
}

// --- entering and leaving -------------------------------------------------

doc::SketchFeature* SketchEditor::Active(Session& session) const {
    return session.doc.FindAs<doc::SketchFeature>(session.activeSketch);
}

void SketchEditor::Reset() {
    myTool = SketchTool::Select;
    myClicks.clear();
    myPicks.clear();
    mySelection.clear();
    myHover = {};
    myDragBefore.reset();
    myDragging = false;
    myPressedOnEntity = false;
    myEditDimension = {};
}

void SketchEditor::CreateAndEdit(AppContext& app, Session& session, const doc::SketchPlacement& placement) {
    auto feature = std::make_unique<doc::SketchFeature>();
    feature->id = session.doc.Ids().Next();
    feature->name = session.doc.UniqueFeatureName("Эскиз");
    feature->placement = placement;
    feature->sketch.EnsureOrigin(session.doc.Ids());
    const ObjectId id = feature->id;
    session.Execute(doc::AddFeature(std::move(feature), session.doc.EndMarker()));
    Edit(app, session, id);
    myTool = SketchTool::Line;   // a new sketch starts with drawing
}

void SketchEditor::Edit(AppContext& app, Session& session, ObjectId sketch) {
    if (!session.doc.FindAs<doc::SketchFeature>(sketch)) return;
    Reset();
    if (session.mode != EditMode::Sketch) myCameraBeforeSketch = app.viewport.Camera();
    session.mode = EditMode::Sketch;
    session.activeSketch = sketch;
    session.selectedFeature = sketch;
    app.shell.ribbonTab = RibbonTab::Sketch;
    LookAtSketch(app, session);
}

void SketchEditor::Finish(AppContext& app, Session& session) {
    // Back to the direction the user looked from before the sketch, framed
    // to show the model and the new sketch (Inventor does the same).
    if (myCameraBeforeSketch) {
        if (const doc::SketchFeature* f = Active(session)) {
            const render::CameraState& before = *myCameraBeforeSketch;
            const render::CameraState target =
                app.viewport.FitCamera(before.center - before.eye, before.up, SketchBox(*f, session.doc.units));
            app.viewport.AnimateTo(target, app.settings.animations ? 0.45 : 0.0);
        }
        myCameraBeforeSketch.reset();
    }
    Reset();
    session.selectedFeature = session.activeSketch;
    session.mode = EditMode::Model;
    session.activeSketch = {};
    app.shell.ribbonTab = RibbonTab::Model;
}

void SketchEditor::LookAtSketch(AppContext& app, Session& session) {
    const doc::SketchFeature* f = Active(session);
    if (!f) return;
    const bz::Frame& frame = f->placement.frame;
    render::CameraState target = app.viewport.FitCamera(-frame.normal, frame.YDir(), SketchBox(*f, session.doc.units));
    // Keep the zoom when the sketch already fits; centre on the sketch plane.
    target.scale = std::max(target.scale, app.viewport.Camera().scale * 0.6);
    app.viewport.AnimateTo(target, app.settings.animations ? 0.45 : 0.0);
}

void SketchEditor::KeepSketchInView(AppContext& app, Session& session) {
    const doc::SketchFeature* f = Active(session);
    if (!f) return;
    const Box2 extents = SketchExtentsWithLabels(*f, session.doc.units);
    if (extents.IsEmpty()) return;
    // Visible rectangle of the sketch plane (the camera looks straight at it).
    const render::CameraState cam = app.viewport.Camera();
    const bz::ViewProjection proj = app.viewport.Projection();
    const bz::Frame& frame = f->placement.frame;
    bool inside = true;
    for (Vec2 corner : {extents.min, extents.max, Vec2{extents.min.x, extents.max.y}, Vec2{extents.max.x, extents.min.y}}) {
        const Vec2 px = proj.Project(frame.ToWorld(corner));
        inside = inside && px.x >= 0 && px.y >= 0 && px.x <= proj.width && px.y <= proj.height;
    }
    if (inside) return;
    const render::CameraState target = app.viewport.FitCamera(cam.center - cam.eye, cam.up, SketchBox(*f, session.doc.units), 1.3);
    app.viewport.AnimateTo(target, app.settings.animations ? 0.3 : 0.0);
}

// --- tools ------------------------------------------------------------------

void SketchEditor::SetTool(AppContext& app, Session& session, SketchTool tool) {
    myClicks.clear();
    myPicks.clear();
    myTool = tool;
    if (tool >= SketchTool::Coincident) TryApplyToSelection(app, session);
}

void SketchEditor::Escape(AppContext& app, Session& session) {
    if (myEditDimension) {
        myEditDimension = {};
    } else if (!myClicks.empty() || !myPicks.empty()) {
        myClicks.clear();
        myPicks.clear();
    } else if (myTool != SketchTool::Select) {
        myTool = SketchTool::Select;
    } else if (!mySelection.empty()) {
        mySelection.clear();
    } else {
        Finish(app, session);
    }
}

void SketchEditor::DeleteSelection(Session& session) {
    doc::SketchFeature* f = Active(session);
    if (!f || mySelection.empty()) return;
    auto before = f->Clone();
    for (ObjectId id : mySelection) f->sketch.Remove(id);
    mySelection.clear();
    myHover = {};
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), "Удаление в эскизе");
}

void SketchEditor::ToggleConstruction(Session& session) {
    doc::SketchFeature* f = Active(session);
    if (!f || mySelection.empty()) return;
    auto before = f->Clone();
    for (ObjectId id : mySelection) {
        if (sketch::Line* l = f->sketch.FindLine(id)) l->construction = !l->construction;
        if (sketch::Circle* c = f->sketch.FindCircle(id)) c->construction = !c->construction;
        for (auto& a : f->sketch.arcs)
            if (a.id == id) a.construction = !a.construction;
    }
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), "Вспомогательная геометрия");
}

// --- per frame ----------------------------------------------------------------

void SketchEditor::Frame(AppContext& app, Session& session, const ViewportFrame& vf) {
    doc::SketchFeature* f = Active(session);
    if (!f) {
        Finish(app, session);
        return;
    }
    const SketchView view{f->placement.frame, vf};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const double step = app.settings.gridStep > 0 ? app.settings.gridStep : AdaptiveGridStep(view.MmPerPixel());
    DrawSketchGrid(app.ui, dl, view, step);

    const ImVec2 mouse = ImGui::GetMousePos();
    myCursor = vf.hovered ? view.FromScreen(mouse) : std::nullopt;
    if (!myDragging) {
        myHover = vf.hovered ? HitTest(f->sketch, view, mouse, session.doc.units) : SketchHit{};
        // Constraint glyphs are selectable too (to delete a constraint).
        if (!myHover && vf.hovered)
            for (const GlyphPlacement& g : LayoutGlyphs(f->sketch, view, app.ui.scale))
                if (std::abs(mouse.x - g.center.x) < app.ui.S(9) && std::abs(mouse.y - g.center.y) < app.ui.S(9))
                    myHover = {g.constraint, sketch::EntityKind::Constraint};
    }

    SketchDrawStyle style;
    style.selection = &mySelection;
    style.hover = myHover.id;
    style.analysis = &f->analysis;
    for (ObjectId id : myPicks) style.picked.push_back(id);
    DrawSketch(app.ui, dl, *f, view, session.doc.units, style);

    if (myEditDimension) HandleInlineEdit(app, session, view);
    if (!vf.hovered && !myDragging) return;

    // A short right click ends a chain of lines, like in KOMPAS.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0 &&
        !myClicks.empty()) {
        myClicks.clear();
    }

    switch (myTool) {
        case SketchTool::Select:    HandleSelect(app, session, view); break;
        case SketchTool::Dimension: HandleDimension(app, session, view); break;
        case SketchTool::Line:
        case SketchTool::Rectangle:
        case SketchTool::Circle:
        case SketchTool::Arc:
        case SketchTool::Point:     HandleDrawing(app, session, view); break;
        default:                    HandleConstraint(app, session, view); break;
    }
    if (doc::SketchFeature* still = Active(session)) DrawPreview(app, view, still->sketch, session.doc.units);
}

void SketchEditor::HandleSelect(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    const ImGuiIO& io = ImGui::GetIO();
    const std::optional<Vec2> cursor = myCursor;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        myPressedOnEntity = static_cast<bool>(myHover);
        // Geometric constraints (glyphs) can be selected but not dragged.
        if (const sketch::Constraint* c = f->sketch.FindConstraint(myHover.id); c && !sketch::IsDimensional(c->type))
            myPressedOnEntity = false;
        myDragEntity = myHover.id;
        if (cursor) myDragStart = *cursor;
    }

    // Double click on a dimension: edit its value.
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && myHover.kind == sketch::EntityKind::Constraint) {
        if (const sketch::Constraint* c = f->sketch.FindConstraint(myHover.id); c && sketch::IsDimensional(c->type))
            StartInlineEdit(*c);
        return;
    }

    // Start dragging after a few pixels.
    if (myPressedOnEntity && !myDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) && cursor) {
        myDragging = true;
        myDragBefore = f->Clone();
        myDragOrigins.clear();
        const auto addPoint = [&](ObjectId p) {
            if (const sketch::Point* pt = f->sketch.FindPoint(p); pt && !pt->isOrigin) myDragOrigins.push_back({p, pt->pos});
        };
        // The grabbed point goes first: the solver falls back to it alone.
        switch (f->sketch.KindOf(myDragEntity)) {
            case sketch::EntityKind::Point: addPoint(myDragEntity); break;
            case sketch::EntityKind::Line: {
                const sketch::Line* l = f->sketch.FindLine(myDragEntity);
                const bool startCloser = (f->sketch.PositionOf(l->start) - myDragStart).Length() <
                                         (f->sketch.PositionOf(l->end) - myDragStart).Length();
                addPoint(startCloser ? l->start : l->end);
                addPoint(startCloser ? l->end : l->start);
                break;
            }
            case sketch::EntityKind::Circle: addPoint(f->sketch.FindCircle(myDragEntity)->center); break;
            case sketch::EntityKind::Arc: {
                const sketch::Arc* a = f->sketch.FindArc(myDragEntity);
                addPoint(a->center);
                addPoint(a->start);
                addPoint(a->end);
                break;
            }
            case sketch::EntityKind::Constraint:
                if (const sketch::Constraint* c = f->sketch.FindConstraint(myDragEntity))
                    if (const auto layout = LayoutDimension(f->sketch, *c, session.doc.units)) myDragLabelStart = layout->label;
                break;
            default: break;
        }
    }

    if (myDragging && cursor) {
        const Vec2 delta = *cursor - myDragStart;
        if (sketch::Constraint* c = f->sketch.FindConstraint(myDragEntity)) {
            // Moving a dimension label: the offset follows the cursor.
            if (const auto anchor = DimensionAnchor(f->sketch, *c)) c->labelOffset = myDragLabelStart + delta - *anchor;
        } else if (!myDragOrigins.empty()) {
            sketch::SolveOptions options;
            for (const auto& d : myDragOrigins) options.drags.push_back({d.point, d.target + delta});
            sketch::Solve(f->sketch, options);
        }
        session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (myDragging) {
            session.CommitInPlaceEdit(std::move(myDragBefore), "Перемещение в эскизе");
        } else if (myHover) {
            // Click: select; Shift or Ctrl add to the selection.
            if (!io.KeyShift && !io.KeyCtrl) mySelection.clear();
            if (myHover) {
                if (mySelection.count(myHover.id)) mySelection.erase(myHover.id);
                else mySelection.insert(myHover.id);
            }
        } else if (!io.KeyShift && !io.KeyCtrl) {
            mySelection.clear();
        }
        myDragging = false;
        myPressedOnEntity = false;
        myDragOrigins.clear();
    }
    (void)app;
    (void)view;
}

void SketchEditor::StartInlineEdit(const sketch::Constraint& c) {
    myEditDimension = c.id;
    myEditText = c.parameter.expression;
    myEditFocus = true;
}

void SketchEditor::HandleInlineEdit(AppContext& app, Session& session, const SketchView& view) {
    doc::SketchFeature* f = Active(session);
    sketch::Constraint* c = f ? f->sketch.FindConstraint(myEditDimension) : nullptr;
    if (!c) {
        myEditDimension = {};
        return;
    }
    const auto layout = LayoutDimension(f->sketch, *c, session.doc.units);
    if (!layout) return;

    ui::Ui& ui = app.ui;
    const ImVec2 at = view.ToScreen(layout->label);
    const float width = ui.S(150);
    ImGui::SetCursorScreenPos({at.x - width / 2, at.y - ui.S(18)});
    const Quantity q = c->type == sketch::ConstraintType::Angle ? Quantity::Angle : Quantity::Length;
    const ui::FieldResult r = ui::ExpressionField(ui, "##dimension-edit", myEditText, UnitLabel(q, session.doc.units), {},
                                                  width, myEditFocus, false);
    myEditFocus = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        myEditDimension = {};
        return;
    }
    if (!r.committed) {
        if (!r.active && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) myEditDimension = {};
        return;
    }

    // Apply the new expression; refuse values that break the sketch.
    auto before = f->Clone();
    const std::string previous = c->parameter.expression;
    c->parameter.expression = myEditText;
    session.doc.EvaluateParameters();
    if (c->parameter.HasError()) {
        app.notes.Error(c->parameter.error);
        c->parameter.expression = previous;
        session.doc.EvaluateParameters();
        return;
    }
    sketch::Sketch trial = f->sketch;
    // The first dimension scales the whole sketch to it, so a sketch drawn at
    // any zoom keeps its proportions instead of being stretched in one place.
    if (IsFirstDimension(trial, *c)) {
        if (const std::optional<double> measured = MeasureDimension(trial, *c); measured && *measured > 1e-9) {
            const double factor = c->parameter.value / *measured;
            if (factor > 0) trial.ScaleAbout({0, 0}, factor);
        }
    }
    if (!sketch::Solve(trial).converged) {
        app.notes.Error("Такой размер противоречит другим ограничениям эскиза");
        c->parameter.expression = previous;
        session.doc.EvaluateParameters();
        return;
    }
    f->sketch = std::move(trial);
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), "Размер " + c->parameter.name);
    myEditDimension = {};
    KeepSketchInView(app, session);
}

std::string SketchEditor::Hint(const Session& session) const {
    (void)session;
    if (myEditDimension) return "Введите значение или выражение, Enter — применить, Esc — отмена";
    const std::size_t step = myClicks.size();
    switch (myTool) {
        case SketchTool::Line:
            return step == 0 ? InfoOf(myTool).hint : "Укажите конечную точку. Правый щелчок или Esc — закончить цепочку";
        case SketchTool::Rectangle: return step == 0 ? InfoOf(myTool).hint : "Укажите противоположный угол";
        case SketchTool::Circle:    return step == 0 ? InfoOf(myTool).hint : "Укажите точку на окружности";
        case SketchTool::Arc:
            return step == 0 ? InfoOf(myTool).hint : step == 1 ? "Укажите начало дуги" : "Укажите конец дуги";
        case SketchTool::Dimension:
            return myPicks.empty() ? InfoOf(myTool).hint : "Укажите место размера или второй элемент";
        default: return InfoOf(myTool).hint;
    }
}

} // namespace bz::app
