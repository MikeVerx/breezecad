#include "app/features/ExtrudeEditor.h"

#include "ui/Widgets.h"

#include <array>

namespace bz::app {

using doc::BooleanOp;
using doc::ExtentType;

bool ExtrudeEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ExtrudeFeature*>(&f) != nullptr; }

std::string ExtrudeEditor::Hint() const {
    return "Щёлкните по контурам эскиза, введите расстояние. Enter или щелчок колесом — готово, Esc — отмена";
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

void ExtrudeEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::ExtrudeFeature&>(feature);
    ui::Ui& ui = app.ui;

    // "Through all" only makes sense for cuts.
    if (f.operation != BooleanOp::Cut && f.extent == ExtentType::ThroughAll) {
        f.extent = ExtentType::Distance;
        Changed(session);
    }

    Section(app, "Направление");
    {
        static constexpr std::array<const char*, 3> labels{"Прямое", "Обратное", "Симметрично"};
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
        Section(app, "Глубина");
        static constexpr std::array<const char*, 2> labels{"На расстояние", "Насквозь"};
        const int current = f.extent == ExtentType::ThroughAll ? 1 : 0;
        const int chosen = ui::Segmented(ui, "depth", labels, current, 2, width);
        if (chosen != current) {
            f.extent = chosen == 1 ? ExtentType::ThroughAll : ExtentType::Distance;
            Changed(session);
        }
        Gap(app);
    }

    if (f.extent != ExtentType::ThroughAll) {
        Section(app, f.extent == ExtentType::Symmetric ? "Расстояние (всего)" : "Расстояние");
        ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
        Gap(app);
    }

    Section(app, "Уклон");
    ParameterField(app, session, "taper", myTaper, f.taper, width, false, false);
}

} // namespace bz::app
