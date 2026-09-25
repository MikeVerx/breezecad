#include "DependencyDialog.h"
#include "occt/OcctView.h"
#include "util/LabelName.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace BreezeCAD::UI::Dialogs {

void DrawDependencyDialog(const DependencyContext& ctx) {
    if (!ctx.state || !ctx.state->open) return;

    static const char* depNames[] = {
        "Совпадение", "Параллельность", "Перпендикулярность",
        "Горизонталь", "Вертикаль", "Касание",
        "Концентричность", "Равенство"
    };
    const char* title = depNames[ctx.state->type % 8];

    ImGuiIO& io = ImGui::GetIO();
    if (!ctx.state->posSet) {
        ctx.state->pos = ImVec2(io.DisplaySize.x * 0.5f - 150,
                                io.DisplaySize.y * 0.5f - 150);
        ctx.state->posSet = true;
    }

    ImGui::SetNextWindowPos(ctx.state->pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Once);

    std::string winTitle = std::string(title) + "##dep_dialog";
    ImGui::Begin(winTitle.c_str(), nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

    ctx.state->pos = ImGui::GetWindowPos();

    // === Базовый объект ===
    ImGui::Text("Базовый объект:");
    ImGui::BeginChild("##base_list", ImVec2(0, 80), true);
    if (ctx.state->base.empty()) {
        ImGui::TextDisabled("(кликните по линии в сцене)");
    } else {
        for (size_t i = 0; i < ctx.state->base.size(); i++) {
            std::string name = Util::GetLabelName(ctx.state->base[i]);
            ImGui::PushID((int)i);
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton("x")) {
                ctx.state->base.erase(ctx.state->base.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // === Зависимые объекты ===
    ImGui::Text("Зависимые объекты:");
    ImGui::BeginChild("##dep_list", ImVec2(0, 80), true);
    if (ctx.state->dependent.empty()) {
        ImGui::TextDisabled("(кликните по линиям в сцене)");
    } else {
        for (size_t i = 0; i < ctx.state->dependent.size(); i++) {
            std::string name = Util::GetLabelName(ctx.state->dependent[i]);
            ImGui::PushID((int)(1000 + i));
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton("x")) {
                ctx.state->dependent.erase(ctx.state->dependent.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();

    // === Кнопки ===
    float btnW = 95.0f;
    if (ImGui::Button("Отмена", ImVec2(btnW, 0))) {
        ctx.state->open = false;
        ctx.state->base.clear();
        ctx.state->dependent.clear();
        if (ctx.view) ctx.view->ClearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Применить", ImVec2(btnW, 0))) {
        // TODO: сохранить зависимость в OCAF
        if (ctx.onApplied) ctx.onApplied(title);
        ctx.state->base.clear();
        ctx.state->dependent.clear();
        if (ctx.view) ctx.view->ClearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Готово", ImVec2(btnW, 0))) {
        ctx.state->open = false;
        ctx.state->base.clear();
        ctx.state->dependent.clear();
        if (ctx.view) ctx.view->ClearSelection();
    }

    ImGui::End();
}

} // namespace BreezeCAD::UI::Dialogs
