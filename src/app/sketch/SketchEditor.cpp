#include "app/sketch/SketchEditor.h"

#include "app/Format.h"
#include "app/sketch/DimensionLayout.h"
#include "app/sketch/SketchOverlay.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "sketch/Solver.h"
#include "ui/Widgets.h"

#include <imgui_stdlib.h>

#include <array>
#include <cmath>

namespace bz::app {
namespace {

constexpr std::array<SketchToolInfo, 32> kTools{{
    {SketchTool::Select, N_("Выбор"), "ui-check", N_("Выберите или перетащите элементы эскиза")},
    {SketchTool::Line, N_("Отрезок"), "sk-line", N_("Укажите начальную точку отрезка")},
    {SketchTool::Rectangle, N_("Прямоугольник"), "sk-rectangle", N_("Укажите первый угол прямоугольника")},
    {SketchTool::Circle, N_("Окружность"), "sk-circle", N_("Укажите центр окружности")},
    {SketchTool::Arc, N_("Дуга"), "sk-arc", N_("Укажите центр дуги")},
    {SketchTool::Point, N_("Точка"), "sk-point", N_("Укажите положение точки")},
    {SketchTool::Spline, N_("Сплайн"), "sk-spline", N_("Щёлкайте точки сплайна. Правый щелчок или Enter — закончить, щелчок по первой точке — замкнуть")},
    {SketchTool::Project, N_("Проецировать"), "sk-project", N_("Щёлкните ребро модели или грань (весь её контур) — проекция появится в эскизе и будет следовать за моделью")},
    {SketchTool::Trim, N_("Обрезка"), "sk-trim", N_("Щёлкните лишний кусок кривой — он удалится до ближайших пересечений")},
    {SketchTool::Extend, N_("Удлинение"), "sk-extend", N_("Щёлкните отрезок у конца — он дотянется до ближайшей кривой")},
    {SketchTool::Offset, N_("Эквидистанта"), "sk-offset", N_("Задайте расстояние и щёлкните контур — копия встанет с той стороны, где курсор")},
    {SketchTool::Split, N_("Разбить"), "sk-split", N_("Щёлкните отрезок или дугу — она разделится на две в этой точке")},
    {SketchTool::Fillet, N_("Скругление"), "sk-fillet", N_("Задайте радиус и щёлкните угол, где сходятся два отрезка")},
    {SketchTool::Move, N_("Перенос"), "sk-move", N_("Выберите элементы (Ctrl+щелчок), затем укажите базовую точку")},
    {SketchTool::Rotate, N_("Поворот"), "sk-rotate", N_("Выберите элементы (Ctrl+щелчок), затем укажите центр поворота")},
    {SketchTool::Copy, N_("Копировать"), "sk-copy", N_("Выберите элементы, затем базовую точку и место копии")},
    {SketchTool::Scale, N_("Масштаб"), "sk-scale", N_("Выберите элементы, щёлкните базовую точку и введите коэффициент")},
    {SketchTool::PatternRect, N_("Прямоугольный"), "sk-pattern-rect", N_("Выберите элементы и задайте число копий и шаг")},
    {SketchTool::PatternCircular, N_("Круговой"), "sk-pattern-circular", N_("Выберите элементы, щёлкните центр и задайте число копий")},
    {SketchTool::Mirror, N_("Зеркало"), "sk-mirror", N_("Выберите элементы, затем «Линия зеркала» и щёлкните линию")},
    {SketchTool::Dimension, N_("Размер"), "sk-dimension", N_("Выберите отрезок, окружность или две точки")},
    {SketchTool::Coincident, N_("Совпадение"), "c-coincident", N_("Выберите две точки или точку и кривую")},
    {SketchTool::Horizontal, N_("Горизонтальность"), "c-horizontal", N_("Выберите отрезок или две точки")},
    {SketchTool::Vertical, N_("Вертикальность"), "c-vertical", N_("Выберите отрезок или две точки")},
    {SketchTool::Parallel, N_("Параллельность"), "c-parallel", N_("Выберите два отрезка")},
    {SketchTool::Perpendicular, N_("Перпендикулярность"), "c-perpendicular", N_("Выберите два отрезка")},
    {SketchTool::Equal, N_("Равенство"), "c-equal", N_("Выберите два отрезка или две окружности")},
    {SketchTool::Tangent, N_("Касательность"), "c-tangent", N_("Выберите кривую и окружность или дугу")},
    {SketchTool::Fix, N_("Фиксация"), "c-fix", N_("Выберите точку, которую нужно закрепить")},
    {SketchTool::Midpoint, N_("Середина"), "c-midpoint", N_("Выберите точку и отрезок")},
    {SketchTool::Concentric, N_("Концентричность"), "c-concentric", N_("Выберите две окружности или дуги")},
    {SketchTool::Symmetric, N_("Симметрия"), "c-symmetric", N_("Выберите две точки или два отрезка, затем линию симметрии")},
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
    feature->name = session.doc.UniqueFeatureName(Tr("Эскиз"));
    feature->placement = placement;
    feature->sketch.EnsureOrigin(session.doc.Ids());
    const ObjectId id = feature->id;
    session.Execute(doc::AddFeature(std::move(feature), session.doc.EndMarker()));
    Edit(app, session, id);
    myTool = SketchTool::Select;   // the user picks the tool
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

Box3 SketchEditor::FitBox(Session& session) const {
    const doc::SketchFeature* f = Active(session);
    return f ? SketchBox(*f, session.doc.units) : Box3{};
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
    myOffsetCurve = {};
    myTool = tool;
    myMirrorAxisStep = tool == SketchTool::Mirror && !mySelection.empty();   // chosen beforehand: go to the line
    if (tool >= SketchTool::Coincident) TryApplyToSelection(app, session);
}

void SketchEditor::Escape(AppContext& app, Session& session) {
    if (myEditDimension) {
        myEditDimension = {};
    } else if (!myClicks.empty() || !myPicks.empty() || myOffsetCurve) {
        myOffsetCurve = {};
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
    f->PruneProjections();
    mySelection.clear();
    myHover = {};
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), Tr("Удаление в эскизе"));
}

void SketchEditor::ToggleConstruction(Session& session) {
    doc::SketchFeature* f = Active(session);
    if (!f || mySelection.empty()) return;
    auto before = f->Clone();
    for (ObjectId id : mySelection) {
        if (sketch::Line* l = f->sketch.FindLine(id)) {
            l->construction = !l->construction;
            l->centerline = false;
        }
        if (sketch::Circle* c = f->sketch.FindCircle(id)) c->construction = !c->construction;
        for (auto& a : f->sketch.arcs)
            if (a.id == id) a.construction = !a.construction;
        for (auto& sp : f->sketch.splines)
            if (sp.id == id) sp.construction = !sp.construction;
    }
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), Tr("Вспомогательная геометрия"));
}

void SketchEditor::UpdateShownGlyphs(const sketch::Sketch& sk, bool all) {
    myShownGlyphs.clear();
    if (all) return;
    // As in Inventor: the constraints of what is selected (and under the
    // cursor) show; the rest stay hidden until F8.
    const auto shown = [&](ObjectId id) {
        return id && (mySelection.contains(id) || id == myHover.id || std::find(myPicks.begin(), myPicks.end(), id) != myPicks.end());
    };
    for (const sketch::Constraint& c : sk.constraints) {
        if (sketch::IsDimensional(c.type)) continue;
        bool related = shown(c.id) || shown(c.refs[0]) || shown(c.refs[1]) || shown(c.refs[2]);
        // A point's constraints show with its curve.
        for (const sketch::Line& l : sk.lines)
            if (shown(l.id) && (c.refs[0] == l.start || c.refs[0] == l.end || c.refs[1] == l.start || c.refs[1] == l.end)) related = true;
        if (related) myShownGlyphs.insert(c.id);
    }
}

void SketchEditor::ToggleCenterline(Session& session) {
    doc::SketchFeature* f = Active(session);
    if (!f || mySelection.empty()) return;
    // All chosen lines become осевые, unless all already are: then back to normal.
    bool all = true;
    for (ObjectId id : mySelection)
        if (const sketch::Line* l = f->sketch.FindLine(id); l && !l->centerline) all = false;
    auto before = f->Clone();
    for (ObjectId id : mySelection)
        if (sketch::Line* l = f->sketch.FindLine(id); l && !l->projected) {
            l->centerline = !all;
            l->construction = false;
        }
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), Tr("Осевая линия"));
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
    UpdateShownGlyphs(f->sketch, app.settings.showAllConstraints);
    const std::unordered_set<ObjectId>* glyphs = app.settings.showAllConstraints ? nullptr : &myShownGlyphs;
    if (!myDragging) {
        myHover = vf.hovered ? HitTest(f->sketch, view, mouse, session.doc.units) : SketchHit{};
        // Constraint glyphs are selectable too (to delete a constraint).
        if (!myHover && vf.hovered)
            for (const GlyphPlacement& g : LayoutGlyphs(f->sketch, view, app.ui.scale, glyphs))
                if (std::abs(mouse.x - g.center.x) < app.ui.S(9) && std::abs(mouse.y - g.center.y) < app.ui.S(9))
                    myHover = {g.constraint, sketch::EntityKind::Constraint};
    }

    SketchDrawStyle style;
    style.selection = &mySelection;
    style.hover = myHover.id;
    style.analysis = &f->analysis;
    style.glyphs = glyphs;
    for (ObjectId id : myPicks) style.picked.push_back(id);
    DrawSketch(app.ui, dl, *f, view, session.doc.units, style);

    myClickUsed = false;
    if (myEditDimension) HandleInlineEdit(app, session, view);
    if (myClickUsed || (!vf.hovered && !myDragging)) return;

    // A short right click ends a chain of lines, like in KOMPAS.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0 &&
        !myClicks.empty()) {
        if (myTool == SketchTool::Spline) FinishSpline(app, session, false);   // a spline ends where it was
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
        case SketchTool::Project:   HandleProject(app, session, view); break;
        case SketchTool::Trim:      HandleTrim(app, session, view); break;
        case SketchTool::Extend:    HandleExtend(app, session, view); break;
        case SketchTool::Split:     HandleSplit(app, session, view); break;
        case SketchTool::Fillet:    HandleFillet(app, session, view); break;
        case SketchTool::Offset:    HandleOffset(app, session, view); break;
        case SketchTool::Spline:    HandleSpline(app, session, view); break;
        case SketchTool::Move:
        case SketchTool::Rotate:    HandleMotion(app, session, view); break;
        case SketchTool::Mirror:    HandleMirror(app, session, view); break;
        case SketchTool::Copy:
        case SketchTool::Scale:
        case SketchTool::PatternRect:
        case SketchTool::PatternCircular: HandleMotion(app, session, view); break;
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
            if (const sketch::Point* pt = f->sketch.FindPoint(p); pt && !pt->isOrigin && !pt->projected) myDragOrigins.push_back({p, pt->pos});
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
            session.CommitInPlaceEdit(std::move(myDragBefore), Tr("Перемещение в эскизе"));
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
    myEditTyped = false;
    myCaretFrames = 0;
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
    // A click on another dimension puts its name into the formula, as in
    // Inventor: "d3" replaces an untouched value, otherwise it is added.
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overField = mouse.x >= at.x - width / 2 && mouse.x <= at.x + width / 2 && mouse.y >= at.y - ui.S(18) &&
                           mouse.y <= at.y + ui.S(18);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && view.vf.hovered && !overField) {
        const SketchHit hit = HitTest(f->sketch, view, mouse, session.doc.units);
        const sketch::Constraint* other = hit.kind == sketch::EntityKind::Constraint ? f->sketch.FindConstraint(hit.id) : nullptr;
        if (other && other->id != c->id && sketch::IsDimensional(other->type) && !other->parameter.name.empty()) {
            if (!myEditTyped) myEditText.clear();
            myEditText += other->parameter.name;
            myEditTyped = true;
            myEditFocus = true;
            myCaretFrames = 2;
            myClickUsed = true;
        }
    }
    const ui::FieldResult r = ui::ExpressionField(ui, "##dimension-edit", myEditText, UnitLabel(q, session.doc.units), {},
                                                  width, myEditFocus, false, myCaretFrames > 0);
    if (myCaretFrames > 0) --myCaretFrames;
    if (r.edited) myEditTyped = true;
    myEditFocus = false;
    if (myClickUsed) return;   // the field lost focus to that click; it comes back next frame
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
        app.notes.Error(Tr(c->parameter.error));
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
        app.notes.Error(Tr("Такой размер противоречит другим ограничениям эскиза"));
        c->parameter.expression = previous;
        session.doc.EvaluateParameters();
        return;
    }
    f->sketch = std::move(trial);
    session.doc.MarkChanged(static_cast<std::size_t>(session.doc.IndexOf(f->id)));
    session.CommitInPlaceEdit(std::move(before), Trf("Размер {}", c->parameter.name));
    myEditDimension = {};
    KeepSketchInView(app, session);
}

std::string SketchEditor::Hint(const Session& session) const {
    (void)session;
    if (myEditDimension) return Tr("Введите значение или выражение, Enter — применить, Esc — отмена");
    const std::size_t step = myClicks.size();
    switch (myTool) {
        case SketchTool::Line:
            return step == 0 ? Tr(InfoOf(myTool).hint) : Tr("Укажите конечную точку. Правый щелчок или Esc — закончить цепочку");
        case SketchTool::Rectangle:
            switch (myOptions.rectangle) {
                case RectangleMode::Polygon:
                    return step == 0 ? Tr("Укажите центр многоугольника")
                                     : myOptions.polygonInscribed ? Tr("Укажите вершину") : Tr("Укажите середину стороны");
                case RectangleMode::ThreePointsCentre:
                    return step == 0 ? Tr("Укажите центр прямоугольника") : step == 1 ? Tr("Укажите середину стороны") : Tr("Укажите ширину");
                case RectangleMode::SlotCentres:
                    return step == 0 ? Tr("Укажите центр первого скругления") : step == 1 ? Tr("Укажите центр второго") : Tr("Укажите ширину паза");
                case RectangleMode::SlotOverall:
                    return step == 0 ? Tr("Укажите один край паза") : step == 1 ? Tr("Укажите другой край") : Tr("Укажите ширину паза");
                case RectangleMode::SlotCentre:
                    return step == 0 ? Tr("Укажите середину паза") : step == 1 ? Tr("Укажите центр скругления") : Tr("Укажите ширину паза");
                case RectangleMode::SlotArc3:
                    return step == 0 ? Tr("Укажите начало дуги паза") : step == 1 ? Tr("Укажите конец")
                         : step == 2 ? Tr("Укажите точку, через которую идёт дуга") : Tr("Укажите ширину паза");
                case RectangleMode::SlotArcCentre:
                    return step == 0 ? Tr("Укажите центр дуги паза") : step == 1 ? Tr("Укажите начало") : step == 2 ? Tr("Укажите конец")
                                                                                                        : Tr("Укажите ширину паза");
                default: break;
            }
            if (myOptions.rectangle == RectangleMode::ThreePoints)
                return step == 0 ? Tr("Укажите начало первой стороны") : step == 1 ? Tr("Укажите конец первой стороны") : Tr("Укажите ширину прямоугольника");
            if (myOptions.rectangle == RectangleMode::Centre)
                return step == 0 ? Tr("Укажите центр прямоугольника") : Tr("Укажите угол прямоугольника");
            return step == 0 ? Tr(InfoOf(myTool).hint) : Tr("Укажите противоположный угол");
        case SketchTool::Circle:
            if (myOptions.circle == CircleMode::ThreePoints)
                return step == 0 ? Tr("Укажите первую точку окружности") : step == 1 ? Tr("Укажите вторую точку") : Tr("Укажите третью точку");
            return step == 0 ? Tr(InfoOf(myTool).hint) : Tr("Укажите точку на окружности");
        case SketchTool::Arc:
            if (myOptions.arc == ArcMode::ThreePoints)
                return step == 0 ? Tr("Укажите начало дуги") : step == 1 ? Tr("Укажите конец дуги") : Tr("Укажите точку, через которую идёт дуга");
            return step == 0 ? Tr(InfoOf(myTool).hint) : step == 1 ? Tr("Укажите начало дуги") : Tr("Укажите конец дуги");
        case SketchTool::Move:
            if (mySelection.empty()) return Tr(InfoOf(myTool).hint);
            return step == 0 ? Tr("Укажите базовую точку (Ctrl+щелчок — изменить выбор)") : Tr("Укажите новое место");
        case SketchTool::Rotate:
            if (mySelection.empty()) return Tr(InfoOf(myTool).hint);
            return step == 0 ? Tr("Укажите центр поворота (Ctrl+щелчок — изменить выбор)")
                             : step == 1 ? Tr("Укажите, откуда поворачивать") : Tr("Укажите, куда повернуть");
        case SketchTool::Dimension:
            return myPicks.empty() ? Tr(InfoOf(myTool).hint) : Tr("Укажите место размера или второй элемент");
        default: return Tr(InfoOf(myTool).hint);
    }
}

} // namespace bz::app
