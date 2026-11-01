#include "app/sketch/SketchRepair.h"

#include "app/AppContext.h"
#include "app/Session.h"
#include "core/I18n.h"
#include "core/Monitor.h"
#include "document/Commands.h"
#include "document/SketchFeature.h"
#include "ui/Menus.h"
#include "ui/Ui.h"

#include <imgui.h>

namespace bz::app {

bool RestoreSketchBeforeBreak(Session& session, ObjectId sketch) {
    const auto* current = session.doc.FindAs<doc::SketchFeature>(sketch);
    if (!current) return false;
    std::unique_ptr<doc::Feature> edited = current->Clone();
    auto* sk = static_cast<doc::SketchFeature*>(edited.get());
    if (!sk->RestoreBeforeBreak(session.doc.Ids())) return false;
    mon::Action("sketch", "восстановление эскиза: " + current->name);
    session.Execute(doc::ReplaceFeature(std::move(edited), N_("Восстановление эскиза")));
    return true;
}

void SketchRepairMenu(AppContext& app, Session& session, ObjectId sketch) {
    ui::Ui& ui = app.ui;
    ui::MenuStyle style(ui);   // before Begin: must outlive the End call
    if (!ImGui::BeginPopup(kSketchRepairPopup)) return;
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(sketch);
    if (sk) {
        if (sk->HasBrokenProjections())
            ui::MenuItem(ui, Tr("Проекция потеряла своё ребро или не сходится с эскизом"), nullptr, "status-warning", false, false);
        else if (!sk->status.message.empty())
            ui::MenuItem(ui, Tr(sk->status.message).c_str(), nullptr, "status-warning", false, false);
        const bool can = !sk->projections.empty() || !sk->contextProjections.empty();
        if (ui::MenuItem(ui, Tr("Восстановить эскиз как до изменения"), nullptr, "ui-undo", false, can)) {
            app.Later([&session, sketch] { RestoreSketchBeforeBreak(session, sketch); });
            ImGui::CloseCurrentPopup();
        }
        // What it costs, under the item: said before it is chosen.
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
        ImGui::TextUnformatted(Tr("Спроецированная геометрия станет независимой"));
        ImGui::PopStyleColor();
    }
    ImGui::EndPopup();
}

} // namespace bz::app
