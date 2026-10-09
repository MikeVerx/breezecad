#include "app/features/RevolveEditor.h"

#include "app/features/SketchAxis.h"

#include "app/sketch/SketchOverlay.h"
#include "app/sketch/SketchPick.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

using doc::RevolveAxisKind;
using doc::RevolveExtent;

/// The axis as two points of the sketch plane (nullopt: its line is gone).
std::optional<std::pair<Vec2, Vec2>> AxisPoints(const doc::RevolveAxis& axis, const sketch::Sketch& sk) {
    switch (axis.kind) {
        case RevolveAxisKind::SketchX: return std::make_pair(Vec2{0, 0}, Vec2{1, 0});
        case RevolveAxisKind::SketchY: return std::make_pair(Vec2{0, 0}, Vec2{0, 1});
        case RevolveAxisKind::SketchLine:
            if (const sketch::Line* l = sk.FindLine(axis.line)) return std::make_pair(sk.PositionOf(l->start), sk.PositionOf(l->end));
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

bool RevolveEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::RevolveFeature*>(&f) != nullptr; }

std::string RevolveEditor::Hint() const {
    return "Щёлкните отрезок эскиза — это ось вращения; щелчок по контуру выбирает профиль. Enter — готово, Esc — отмена";
}

std::unique_ptr<doc::SketchSolidFeature> RevolveEditor::Create(Session& session, ObjectId sketch) const {
    auto f = std::make_unique<doc::RevolveFeature>();
    f->angle.name = session.doc.NextParameterName();
    f->angle.expression = "90";
    // The осевая of the sketch is the axis; failing that, a helper line.
    f->axis = {RevolveAxisKind::SketchLine, {}};
    if (const auto* sk = session.doc.FindAs<doc::SketchFeature>(sketch)) f->axis.line = DefaultAxisLine(sk->sketch);
    return f;
}

void RevolveEditor::LoadTexts(const doc::Feature& feature) {
    myAngle = static_cast<const doc::RevolveFeature&>(feature).angle.expression;
}

void RevolveEditor::CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const {
    const auto& prev = static_cast<const doc::RevolveFeature&>(previous);
    auto& f = static_cast<doc::RevolveFeature&>(next);
    f.axis = prev.axis;
    f.extent = prev.extent;
    f.reversed = prev.reversed;
}

void RevolveEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::RevolveFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, "Ось");
    {
        const auto* sk = session.doc.FindAs<doc::SketchFeature>(f.sketch);
        const sketch::Line* line = sk && f.axis.kind == RevolveAxisKind::SketchLine ? sk->sketch.FindLine(f.axis.line) : nullptr;
        SketchAxisChip(app, line, f.axis.kind != RevolveAxisKind::SketchLine, width);
    }
    Gap(app);

    Section(app, "Угол");
    {
        static constexpr std::array<const char*, 3> labels{"Полный", "Угол", "Симметрично"};
        const int current = static_cast<int>(f.extent);
        const int chosen = ui::Segmented(ui, "extent", labels, current, 3, width);
        if (chosen != current) {
            f.extent = static_cast<RevolveExtent>(chosen);
            Changed(session);
        }
    }
    if (f.extent != RevolveExtent::Full) {
        ImGui::Dummy({0, ui.S(6)});
        ParameterField(app, session, "angle", myAngle, f.angle, width, myFocusFirstField);
    }
    if (f.extent == RevolveExtent::Angle) {
        Gap(app);
        Section(app, "Направление");
        static constexpr std::array<const char*, 2> labels{"Прямое", "Обратное"};
        const int current = f.reversed ? 1 : 0;
        const int chosen = ui::Segmented(ui, "dir", labels, current, 2, width);
        if (chosen != current) {
            f.reversed = chosen == 1;
            Changed(session);
        }
    }
}

bool RevolveEditor::PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) {
    auto& f = static_cast<doc::RevolveFeature&>(feature);
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f.sketch);
    if (!sk) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::ThemeColors& c = app.ui.C();

    if (const auto axis = AxisPoints(f.axis, sk->sketch))
        DrawSketchAxis(app, dl, view, axis->first, axis->second, c.accent, 1.6f);
    // A line under the cursor can become the axis.
    const std::optional<ObjectId> picked = PickSketchLine(app, session, sk->sketch, view);
    if (!picked) return view.vf.hovered && HitTest(sk->sketch, view, ImGui::GetMousePos(), session.doc.units).kind == sketch::EntityKind::Line;
    f.axis = {RevolveAxisKind::SketchLine, *picked};
    Changed(session);
    return true;
}

} // namespace bz::app
