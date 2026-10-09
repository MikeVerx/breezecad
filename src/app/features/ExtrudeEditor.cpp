#include "app/features/ExtrudeEditor.h"

#include "app/Format.h"
#include "app/sketch/SketchView.h"
#include "core/I18n.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>

namespace bz::app {

using doc::BooleanOp;
using doc::ExtentType;

bool ExtrudeEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ExtrudeFeature*>(&f) != nullptr; }

std::string ExtrudeEditor::Hint() const {
    return Tr("Щёлкните по контурам эскиза, введите расстояние. Enter или щелчок колесом — готово, Esc — отмена");
}

std::unique_ptr<doc::SketchSolidFeature> ExtrudeEditor::Create(Session& session, ObjectId sketch) const {
    (void)sketch;
    auto f = std::make_unique<doc::ExtrudeFeature>();
    f->distance.name = session.doc.NextParameterName();
    f->distance.expression = "10";
    return f;
}

void ExtrudeEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::ExtrudeFeature&>(feature);
    myDistance = f.distance.expression;
    myTaper = f.taper.expression;
}

void ExtrudeEditor::CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const {
    const auto& prev = static_cast<const doc::ExtrudeFeature&>(previous);
    auto& f = static_cast<doc::ExtrudeFeature&>(next);
    f.extent = prev.extent;
    f.reversed = prev.reversed;
}

bool ExtrudeEditor::PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) {
    auto& f = static_cast<doc::ExtrudeFeature&>(feature);
    if (f.extent == ExtentType::ThroughAll || f.distance.HasError()) {
        myArrow.Release();
        return false;
    }
    const std::optional<Vec2> middle = ProfileMiddle(session, f);
    if (!middle) {
        myArrow.Release();
        return false;
    }
    const bool symmetric = f.extent == ExtentType::Symmetric;
    const Vec3 dir = view.frame.normal * (f.reversed ? -1.0 : 1.0);
    const double reach = symmetric ? f.distance.value / 2 : f.distance.value;
    const std::string label = FormatValue(f.distance.value, Quantity::Length, session.doc.units, 2) + " " +
                              UnitLabel(Quantity::Length, session.doc.units);
    if (auto along = myArrow.Update(app, view.vf, view.frame.ToWorld(*middle), dir, reach, label)) {
        // Dragged past the sketch plane: the other direction.
        if (*along < 0 && !symmetric) {
            f.reversed = !f.reversed;
            *along = -*along;
            myArrow.Flipped();
        }
        const double step = DragStep(view.MmPerPixel());
        const double total = std::max(symmetric ? 2 * *along : *along, step);
        myDistance = FormatValue(total, Quantity::Length, session.doc.units, 3);
        f.distance.expression = myDistance;
        Changed(session);
    }
    return myArrow.Hot();
}

void ExtrudeEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::ExtrudeFeature&>(feature);
    ui::Ui& ui = app.ui;

    // "Through all" only makes sense for cuts.
    if (f.operation != BooleanOp::Cut && f.extent == ExtentType::ThroughAll) {
        f.extent = ExtentType::Distance;
        Changed(session);
    }

    Section(app, Tr("Направление"));
    {
        static constexpr std::array<const char*, 3> labels{N_("Прямое"), N_("Обратное"), N_("Симметрично")};
        const int current = f.extent == ExtentType::Symmetric ? 2 : f.reversed ? 1 : 0;
        const int chosen = ui::Segmented(ui, "dir", labels, current, 3, width);
        if (chosen != current) {
            f.reversed = chosen == 1;
            if (chosen == 2) f.extent = ExtentType::Symmetric;
            else if (f.extent == ExtentType::Symmetric) f.extent = ExtentType::Distance;
            Changed(session);
        }
    }
    Gap(app);

    // Depth for cuts: a distance or through everything.
    if (f.operation == BooleanOp::Cut) {
        Section(app, Tr("Глубина"));
        static constexpr std::array<const char*, 2> labels{N_("На расстояние"), N_("Насквозь")};
        const int current = f.extent == ExtentType::ThroughAll ? 1 : 0;
        const int chosen = ui::Segmented(ui, "depth", labels, current, 2, width);
        if (chosen != current) {
            f.extent = chosen == 1 ? ExtentType::ThroughAll : ExtentType::Distance;
            Changed(session);
        }
        Gap(app);
    }

    if (f.extent != ExtentType::ThroughAll) {
        Section(app, f.extent == ExtentType::Symmetric ? Tr("Расстояние (всего)") : Tr("Расстояние"));
        ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
        Gap(app);
    }

    Section(app, Tr("Уклон"));
    ParameterField(app, session, "taper", myTaper, f.taper, width, false, false);
}

} // namespace bz::app
