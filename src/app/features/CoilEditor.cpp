#include "app/features/CoilEditor.h"

#include "ui/Widgets.h"

#include <algorithm>
#include <array>

namespace bz::app {

bool CoilEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::CoilFeature*>(&f) != nullptr; }

std::string CoilEditor::Hint() const {
    return "Выберите ось, направление навивки и размеры. Enter — готово, Esc — отмена";
}

void CoilEditor::StartNew(AppContext& app, Session& session) {
    auto f = std::make_unique<doc::CoilFeature>();
    f->radius.name = session.doc.NextParameterName();
    OpenNew(app, session, std::move(f));
}

void CoilEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::CoilFeature&>(feature);
    myRadius = f.radius.expression;
    myPitch = f.pitch.expression;
    myTurns = f.turns.expression;
    myWire = f.wire.expression;
    myStart = f.start.expression;
}

void CoilEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::CoilFeature&>(feature);
    ui::Ui& ui = app.ui;

    Section(app, "Операция");
    static constexpr std::array<const char*, 3> ops{"Новое тело", "Добавить", "Вырезать"};
    const bool bodyBefore = HasSolidBefore(session.doc, static_cast<std::size_t>(session.doc.IndexOf(f.id)));
    const int currentOp = std::min(static_cast<int>(f.operation), 2);
    const int chosenOp = ui::Segmented(ui, "op", ops, currentOp, 3, width, bodyBefore ? 0 : 0b110);
    if (chosenOp != currentOp) {
        f.operation = static_cast<doc::BooleanOp>(chosenOp);
        Changed(session);
    }
    Caption(app, "«Вырезать» по телу даёт канавку, «Добавить» — выступ, как простая резьба.", width);
    Gap(app);

    Section(app, "Ось (через начало координат)");
    static constexpr std::array<const char*, 3> axes{"X", "Y", "Z"};
    const int currentAxis = static_cast<int>(f.axis);
    const int chosenAxis = ui::Segmented(ui, "axis", axes, currentAxis, 3, width);
    if (chosenAxis != currentAxis) {
        f.axis = static_cast<doc::ModelAxis>(chosenAxis);
        Changed(session);
    }
    Gap(app);

    Section(app, "Навивка");
    static constexpr std::array<const char*, 2> hands{"Правая", "Левая"};
    const int currentHand = f.leftHanded ? 1 : 0;
    const int chosenHand = ui::Segmented(ui, "hand", hands, currentHand, 2, width);
    if (chosenHand != currentHand) {
        f.leftHanded = chosenHand == 1;
        Changed(session);
    }
    Gap(app);

    Section(app, "Радиус навивки");
    ParameterField(app, session, "radius", myRadius, f.radius, width, false);
    Section(app, "Шаг");
    ParameterField(app, session, "pitch", myPitch, f.pitch, width, false);
    Section(app, "Число витков");
    ParameterField(app, session, "turns", myTurns, f.turns, width, false);
    Section(app, "Диаметр проволоки");
    ParameterField(app, session, "wire", myWire, f.wire, width, false);
    Section(app, "Начало вдоль оси");
    ParameterField(app, session, "start", myStart, f.start, width, false);
}

} // namespace bz::app
