#include "app/features/ThreadEditor.h"
#include "app/features/ToolChoice.h"

#include "app/Format.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>
#include <format>

namespace bz::app {

std::string ThreadDesignation(doc::ThreadStandard standard, double nominal, double pitch, const UnitSystem& units) {
    const auto mm = [&](double v) { return FormatValue(v, Quantity::Length, units); };
    switch (standard) {
        case doc::ThreadStandard::PipeG:
            if (const std::string name = doc::PipeSizeName(nominal); !name.empty()) return name;
            return "G\xC3\x98" + mm(nominal);   // not a standard size: by its diameter
        case doc::ThreadStandard::Trapezoidal: return "Tr" + mm(nominal) + "\xC3\x97" + mm(pitch);
        case doc::ThreadStandard::Metric:      break;
    }
    return "M" + mm(nominal) + "\xC3\x97" + mm(pitch);
}

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
    myClearance = f.spec.clearance.expression;
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
        const double pitch = doc::StandardPitch(f.spec.standard, f.size > 0 ? f.size : f.site->nominal);
        if (std::abs(pitch - f.pitch.value) > 1e-9) {
            myPitch = FormatValue(pitch, Quantity::Length, session.doc.units);
            f.pitch.expression = myPitch;
            Changed(session);
        }
    }

    Section(app, Tr("Грань"));
    std::string size;
    if (f.site)
        size = std::format("{} {}", ThreadDesignation(f.spec.standard, f.size > 0 ? f.size : f.site->nominal, f.pitch.value, session.doc.units),
                           f.site->external ? Tr("наружная") : Tr("внутренняя"));
    else if (f.face)
        size = Tr("выбрана");
    PickChip(ui, "##face", Tr("Цилиндр"), size, true, width);
    Caption(app, Tr("На валу резьба врезается внутрь, в отверстии — в стенку: отверстие считается диаметром под метчик."), width);
    Gap(app);

    // Kind, size, fit, lead-in chamfer.
    if (ThreadControls(app, session, f.spec, f.size, f.site ? f.site->nominal : 0.0, myClearance, width, true)) myAutoPitch = true;
    if (f.size > 0 && f.site && std::abs(f.site->nominal - f.size) > 1e-6) {
        // Too much material is cut away; too little is added, so the thread is whole.
        const bool bigger = f.size > f.site->nominal;
        const auto name = [&](double d) {
            return ThreadDesignation(f.spec.standard, d, doc::StandardPitch(f.spec.standard, d), session.doc.units);
        };
        const std::string was = name(f.site->nominal), now = name(f.size);
        const std::string note =
            f.site->external
                ? (bigger ? Trf("Вал под {}, выбрана {}: недостающий материал добавится, резьба будет полной.", was, now)
                          : Trf("Вал под {}, выбрана {}: лишнее снаружи срежется.", was, now))
                : (bigger ? Trf("Отверстие под {}, выбрана {}: оно будет расточено.", was, now)
                          : Trf("Отверстие под {}, выбрана {}: стенка нарастёт, резьба будет полной, не обрезанной.", was, now));
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
