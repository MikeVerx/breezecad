#include "app/features/CoilEditor.h"
#include "app/features/ToolChoice.h"

#include "app/features/SketchAxis.h"
#include "app/sketch/SketchPick.h"
#include "core/I18n.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {

bool CoilEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::CoilFeature*>(&f) != nullptr; }

std::string CoilEditor::Hint() const {
    return Tr("Щёлкните осевую линию эскиза — вокруг неё навивается профиль; задайте шаг и число витков. Enter — готово, Esc — отмена");
}

std::unique_ptr<doc::SketchSolidFeature> CoilEditor::Create(Session& session, ObjectId sketch) const {
    auto f = std::make_unique<doc::CoilFeature>();
    f->pitch.name = session.doc.NextParameterName();
    if (const auto* sk = session.doc.FindAs<doc::SketchFeature>(sketch)) f->axisLine = DefaultAxisLine(sk->sketch);
    return f;
}

void CoilEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::CoilFeature&>(feature);
    myPitch = f.pitch.expression;
    myTurns = f.turns.expression;
}

void CoilEditor::CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const {
    const auto& prev = static_cast<const doc::CoilFeature&>(previous);
    auto& f = static_cast<doc::CoilFeature&>(next);
    f.leftHanded = prev.leftHanded;
}

void CoilEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::CoilFeature&>(feature);
    Section(app, Tr("Ось"));
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f.sketch);
    SketchAxisChip(app, sk ? sk->sketch.FindLine(f.axisLine) : nullptr, false, width);
    Gap(app);

    Section(app, Tr("Шаг"));
    ParameterField(app, session, "pitch", myPitch, f.pitch, width, myFocusFirstField);
    Section(app, Tr("Число витков"));
    ParameterField(app, session, "turns", myTurns, f.turns, width, false);
    Gap(app);

    static constexpr std::array<ChoiceItem, 2> hands{{{nullptr, N_("Правая")}, {nullptr, N_("Левая")}}};
    const int current = f.leftHanded ? 1 : 0;
    const int chosen = ToolChoice(app, "hand", N_("Навивка"), hands, current, width);
    if (chosen != current) {
        f.leftHanded = chosen == 1;
        Changed(session);
    }
}

bool CoilEditor::PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) {
    auto& f = static_cast<doc::CoilFeature&>(feature);
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f.sketch);
    if (!sk) return false;
    if (const sketch::Line* line = sk->sketch.FindLine(f.axisLine))
        DrawSketchAxis(app, ImGui::GetWindowDrawList(), view, sk->sketch.PositionOf(line->start), sk->sketch.PositionOf(line->end),
                       app.ui.C().accent, 1.6f);
    const std::optional<ObjectId> picked = PickSketchLine(app, session, sk->sketch, view);
    if (!picked) return view.vf.hovered && HitTest(sk->sketch, view, ImGui::GetMousePos(), session.doc.units).kind == sketch::EntityKind::Line;
    f.axisLine = *picked;
    Changed(session);
    return true;
}

} // namespace bz::app
