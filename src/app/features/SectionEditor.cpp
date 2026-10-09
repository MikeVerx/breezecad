#include "app/features/SectionEditor.h"

#include "document/Origin.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {

bool SectionEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::SectionFeature*>(&f) != nullptr; }

std::string SectionEditor::Hint() const {
    return "Выберите плоскость и сторону, которая останется. Enter — готово, Esc — отмена";
}

void SectionEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info("Сначала создайте тело: эскиз и выдавливание или вращение");
        return;
    }
    auto f = std::make_unique<doc::SectionFeature>();
    f->plane = doc::origin::kPlaneXZ;   // the front plane: the usual cut to look inside
    OpenNew(app, session, std::move(f));
}

void SectionEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    if (auto* f = dynamic_cast<doc::SectionFeature*>(Edited(session))) PickPlane(app, session, f->plane, vf);
}

void SectionEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::SectionFeature&>(feature);
    Section(app, "Плоскость");
    PlaneChoice(app, session, f.plane, width);
    Gap(app);

    Section(app, "Остаётся");
    static constexpr std::array<const char*, 2> sides{"За плоскостью", "Перед плоскостью"};
    const int current = f.flipped ? 1 : 0;
    const int chosen = ui::Segmented(app.ui, "side", sides, current, 2, width);
    if (chosen != current) {
        f.flipped = chosen == 1;
        Changed(session);
    }
    Gap(app);
    Caption(app,
            "Сечение — обычная операция дерева: погасите её, чтобы увидеть деталь целиком, или удалите. "
            "Рёбра и грани среза можно выбирать для следующих операций.",
            width);
}

} // namespace bz::app
