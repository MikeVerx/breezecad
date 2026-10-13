#include "app/features/ShellEditor.h"

#include "core/I18n.h"
#include "document/ShellFeature.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {

void ShellEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info(Tr("Сначала создайте тело: эскиз и выдавливание или вращение"));
        return;
    }
    auto f = std::make_unique<doc::ShellFeature>();
    f->thickness.name = session.doc.NextParameterName();
    OpenNew(app, session, std::move(f));
}

bool ShellEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ShellFeature*>(&f) != nullptr; }

std::string ShellEditor::Hint() const {
    return Tr("Щёлкните грани, которые нужно удалить (повторный щелчок возвращает грань), введите толщину. Enter — готово, Esc — отмена");
}

void ShellEditor::LoadTexts(const doc::Feature& feature) {
    myThickness = static_cast<const doc::ShellFeature&>(feature).thickness.expression;
}

void ShellEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::ShellFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, Tr("Удаляемые грани"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, "ui-check", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        const std::string chip = f.faces.empty() ? std::string(Tr("Нет — оболочка замкнутая"))
                                                 : Trf("Удалено граней: {}", f.faces.size());
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({width, h});
    }
    Caption(app, Tr("Щёлкните грань на модели, чтобы удалить её (оставить открытой); повторный щелчок возвращает."), width);
    if (!f.faces.empty() && ui::LinkButton(ui, "##clear-faces", Tr("Очистить выбор"))) {
        f.faces.clear();
        Changed(session);
    }
    Gap(app);

    Section(app, Tr("Толщина стенки"));
    ParameterField(app, session, "thickness", myThickness, f.thickness, width, myFocusFirstField);
    Gap(app);

    Section(app, Tr("Направление"));
    static constexpr std::array<const char*, 2> sides{N_("Внутрь"), N_("Наружу")};
    const int current = f.outward ? 1 : 0;
    if (const int chosen = ui::Segmented(ui, "side", sides, current, 2, width); chosen != current && chosen >= 0) {
        f.outward = chosen == 1;
        Changed(session);
    }
    Caption(app, f.outward ? Tr("Стенки растут наружу: внутренние размеры тела сохраняются.")
                           : Tr("Стенки растут внутрь: наружные размеры тела сохраняются."),
            width);
}

void ShellEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::ShellFeature*>(Edited(session));
    if (!f) return;
    PickOriginalFaces(app, session, f->faces, vf);
}

} // namespace bz::app
