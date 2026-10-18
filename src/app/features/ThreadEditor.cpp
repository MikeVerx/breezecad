#include "app/features/ThreadEditor.h"
#include "app/features/ToolChoice.h"

#include "app/Format.h"
#include "ui/Menus.h"
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

    // The standard pitch of the size (chosen, or measured by the rebuild).
    if (myAutoPitch && (f.site || f.size > 0)) {
        myAutoPitch = false;
        const double pitch = doc::CoarsePitch(f.size > 0 ? f.size : f.site->nominal);
        if (std::abs(pitch - f.pitch.value) > 1e-9) {
            myPitch = FormatValue(pitch, Quantity::Length, session.doc.units);
            f.pitch.expression = myPitch;
            Changed(session);
        }
    }

    const auto mm = [&](double v) { return FormatValue(v, Quantity::Length, session.doc.units); };
    Section(app, Tr("Грань"));
    std::string size;
    if (f.site)
        size = std::format("M{}\xC3\x97{} {}", mm(f.size > 0 ? f.size : f.site->nominal), mm(f.pitch.value),
                           f.site->external ? Tr("наружная") : Tr("внутренняя"));
    else if (f.face)
        size = Tr("выбрана");
    PickChip(ui, "##face", Tr("Цилиндр"), size, true, width);
    Caption(app, Tr("На валу резьба врезается внутрь, в отверстии — в стенку: отверстие считается диаметром под метчик."), width);
    Gap(app);

    // The standard size: by the face, or chosen from the list.
    Section(app, Tr("Размер"));
    const std::string chosen = f.size > 0 ? "M" + mm(f.size) : std::string(Tr("По грани"));
    if (PickChip(ui, "##size", Tr("Резьба"), chosen, false, width)) ImGui::OpenPopup("##sizes");
    {
        ui::MenuStyle style(ui);   // before Begin: must outlive the End call
        if (ImGui::BeginPopup("##sizes")) {
            if (ui::MenuItem(ui, Tr("По грани"), nullptr, {}, f.size <= 0)) {
                f.size = 0;
                myAutoPitch = true;
                Changed(session);
            }
            for (double d : doc::StandardSizes()) {
                const std::string label = std::format("M{}\xC3\x97{}", mm(d), mm(doc::CoarsePitch(d)));
                if (ui::MenuItem(ui, label.c_str(), nullptr, {}, std::abs(f.size - d) < 1e-9)) {
                    f.size = d;
                    myAutoPitch = true;
                    Changed(session);
                }
            }
            ImGui::EndPopup();
        }
    }
    if (f.size > 0 && f.site && std::abs(f.site->nominal - f.size) > 1e-6) {
        const std::string note = f.site->external
                                     ? Trf("Вал под M{}, выбрана M{}: лишнее снаружи срежется, резьба будет M{}.", mm(f.site->nominal), mm(f.size), mm(f.size))
                                     : Trf("Отверстие под M{}, выбрана M{}: оно будет расточено под M{}.", mm(f.site->nominal), mm(f.size), mm(f.size));
        Caption(app, note.c_str(), width);
    }
    Gap(app);

    {
        static constexpr std::array<ChoiceItem, 2> kinds{{{"thread-real", N_("Настоящая")}, {"thread-cosmetic", N_("Условная")}}};
        const int current = f.cosmetic ? 1 : 0;
        if (const int picked = ToolChoice(app, "kind", N_("Вид"), kinds, current, width); picked != current) {
            f.cosmetic = picked == 1;
            Changed(session);
        }
        Caption(app, f.cosmetic ? Tr("Резьба только нарисована на грани: модель легче и быстрее. В файлы STEP, STL и 3MF она "
                                     "будет вырезана настоящей. Проверка коллизий, масса и сечения считают гладкий цилиндр.")
                                : Tr("Витки вырезаются в теле. Две настоящие резьбы, вкрученные друг в друга, проверка "
                                     "коллизий может показать как пересечение — для сборок удобнее условная."),
                width);
    }
    Gap(app);

    Section(app, Tr("Шаг"));
    if (ParameterField(app, session, "pitch", myPitch, f.pitch, width, myFocusFirstField)) myAutoPitch = false;
    Gap(app);

    {
        static constexpr std::array<ChoiceItem, 2> labels{{{"thread-full", N_("Вся грань")}, {"thread-length", N_("На длину")}}};
        const int current = f.fullLength ? 0 : 1;
        if (const int chosen = ToolChoice(app, "extent", N_("Длина"), labels, current, width); chosen != current) {
            f.fullLength = chosen == 0;
            Changed(session);
        }
    }
    if (!f.fullLength) {
        ImGui::Dummy({0, ui.S(6)});
        ParameterField(app, session, "length", myLength, f.length, width, false);
        ImGui::Dummy({0, ui.S(6)});
        static constexpr std::array<ChoiceItem, 2> ends{{{"thread-from-start", N_("С начала")}, {"thread-from-end", N_("С другого конца")}}};
        const int current = f.fromOtherEnd ? 1 : 0;
        if (const int chosen = ToolChoice(app, "end", N_("Откуда"), ends, current, width); chosen != current) {
            f.fromOtherEnd = chosen == 1;
            Changed(session);
        }
    }
    Gap(app);

    static constexpr std::array<ChoiceItem, 2> hands{{{"hand-right", N_("Правая")}, {"hand-left", N_("Левая")}}};
    const int current = f.leftHanded ? 1 : 0;
    if (const int chosen = ToolChoice(app, "hand", N_("Направление"), hands, current, width); chosen != current) {
        f.leftHanded = chosen == 1;
        Changed(session);
    }
}

} // namespace bz::app
