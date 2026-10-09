#include "app/features/ShellEditor.h"

#include "ui/Widgets.h"

#include <array>

namespace bz::app {

bool ShellEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ShellFeature*>(&f) != nullptr; }

std::string ShellEditor::Hint() const {
    return "Щёлкните плоские грани, которые станут отверстиями (повторный щелчок возвращает грань), введите толщину. "
           "Enter — готово, Esc — отмена";
}

void ShellEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info("Сначала создайте тело: эскиз и выдавливание или вращение");
        return;
    }
    auto f = std::make_unique<doc::ShellFeature>();
    f->thickness.name = session.doc.NextParameterName();
    OpenNew(app, session, std::move(f));
}

void ShellEditor::LoadTexts(const doc::Feature& feature) {
    myThickness = static_cast<const doc::ShellFeature&>(feature).thickness.expression;
}

void ShellEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    if (auto* f = dynamic_cast<doc::ShellFeature*>(Edited(session))) PickOriginalFaces(app, session, f->openFaces, vf);
}

void ShellEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::ShellFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, "Открытые грани");
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, "ui-check", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        const std::string chip = f.openFaces.empty() ? std::string("Нет — оболочка закрытая")
                                                     : "Выбрано граней: " + std::to_string(f.openFaces.size());
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({width, h});
    }
    Caption(app, "Щёлкните плоскую грань, чтобы удалить её и открыть полость; повторный щелчок возвращает грань.", width);
    if (!f.openFaces.empty() && ui::LinkButton(ui, "##clear-faces", "Очистить выбор")) {
        f.openFaces.clear();
        Changed(session);
    }
    Gap(app);

    Section(app, "Толщина стенки");
    ParameterField(app, session, "thickness", myThickness, f.thickness, width, myFocusFirstField);
    Gap(app);

    Section(app, "Стенки растут");
    static constexpr std::array<const char*, 2> labels{"Внутрь", "Наружу"};
    const int current = f.outward ? 1 : 0;
    const int chosen = ui::Segmented(ui, "side", labels, current, 2, width);
    if (chosen != current) {
        f.outward = chosen == 1;
        Changed(session);
    }
    Caption(app, f.outward ? "Деталь становится полостью, стенки снаружи." : "Наружные размеры детали сохраняются.", width);
}

} // namespace bz::app
