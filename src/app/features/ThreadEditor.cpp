#include "app/features/ThreadEditor.h"

#include "app/Format.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>
#include <format>

namespace bz::app {

bool ThreadEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ThreadFeature*>(&f) != nullptr; }

std::string ThreadEditor::Hint() const {
    return Tr("Щёлкните цилиндрическую грань: вал или стенку отверстия. Enter — готово, Esc — отмена");
}

void ThreadEditor::StartNew(AppContext& app, Session& session) {
    OpenNew(app, session, std::make_unique<doc::ThreadFeature>());
    myAutoPitch = false;
}

void ThreadEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::ThreadFeature&>(feature);
    myPitch = f.pitch.expression;
    myLength = f.length.expression;
    myAutoPitch = false;
}

void ThreadEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::ThreadFeature*>(Edited(session));
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Faces);
    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (const auto c = app.viewport.PickCylinder(vf.ToLocal(ImGui::GetMousePos()))) {
        f->face = doc::CylinderTouch{c->axisPoint, c->axisDir, c->radius, c->radial};
        myAutoPitch = true;
        Changed(session);
    } else {
        app.notes.Info(Tr("Нужна цилиндрическая грань"));
    }
}

void ThreadEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::ThreadFeature&>(feature);
    ui::Ui& ui = app.ui;

    // The standard pitch of the picked size, once the rebuild has measured it.
    if (myAutoPitch && f.site) {
        myAutoPitch = false;
        const double pitch = doc::CoarsePitch(f.site->nominal);
        if (std::abs(pitch - f.pitch.value) > 1e-9) {
            myPitch = FormatValue(pitch, Quantity::Length, session.doc.units);
            f.pitch.expression = myPitch;
            Changed(session);
        }
    }

    Section(app, Tr("Грань"));
    std::string size;
    if (f.site)
        size = std::format("M{}\xC3\x97{} {}", FormatValue(f.site->nominal, Quantity::Length, session.doc.units),
                           FormatValue(f.pitch.value, Quantity::Length, session.doc.units),
                           f.site->external ? Tr("наружная") : Tr("внутренняя"));
    else if (f.face)
        size = Tr("выбрана");
    PickChip(ui, "##face", Tr("Цилиндр"), size, true, width);
    Caption(app, Tr("На валу резьба врезается внутрь, в отверстии — в стенку: отверстие считается диаметром под метчик."), width);
    Gap(app);

    Section(app, Tr("Шаг"));
    if (ParameterField(app, session, "pitch", myPitch, f.pitch, width, myFocusFirstField)) myAutoPitch = false;
    Gap(app);

    Section(app, Tr("Длина"));
    {
        static constexpr std::array<const char*, 2> labels{N_("Вся грань"), N_("На длину")};
        const int current = f.fullLength ? 0 : 1;
        if (const int chosen = ui::Segmented(ui, "extent", labels, current, 2, width); chosen != current) {
            f.fullLength = chosen == 0;
            Changed(session);
        }
    }
    if (!f.fullLength) {
        ImGui::Dummy({0, ui.S(6)});
        ParameterField(app, session, "length", myLength, f.length, width, false);
        ImGui::Dummy({0, ui.S(6)});
        static constexpr std::array<const char*, 2> ends{N_("С начала"), N_("С другого конца")};
        const int current = f.fromOtherEnd ? 1 : 0;
        if (const int chosen = ui::Segmented(ui, "end", ends, current, 2, width); chosen != current) {
            f.fromOtherEnd = chosen == 1;
            Changed(session);
        }
    }
    Gap(app);

    Section(app, Tr("Направление"));
    static constexpr std::array<const char*, 2> hands{N_("Правая"), N_("Левая")};
    const int current = f.leftHanded ? 1 : 0;
    if (const int chosen = ui::Segmented(ui, "hand", hands, current, 2, width); chosen != current) {
        f.leftHanded = chosen == 1;
        Changed(session);
    }
}

} // namespace bz::app
