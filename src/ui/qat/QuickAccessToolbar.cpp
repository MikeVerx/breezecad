#include "QuickAccessToolbar.h"
#include "app/IconManager.h"
#include "ui/widgets/SvgButton.h"
#include "ui/widgets/Tooltip.h"

#include "imgui.h"

#include <cctype>
#include <cstring>
#include <utility>

namespace BreezeCAD::UI::Qat {

void DrawQuickAccessToolbar(const QatContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));

    // === 1. Логотип BreezeCAD — микро-меню ===
    // === 1. Логотип — меню формата окна ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_logo", "breeze-logo", 24, "BreezeCAD")) {
        ImGui::OpenPopup("##logo_menu");
    }
    if (ImGui::BeginPopup("##logo_menu")) {
        if (ImGui::MenuItem("Свернуть")) {
            if (ctx.onMinimize) ctx.onMinimize();
        }
        if (ImGui::MenuItem("Развернуть")) {
            if (ctx.onMaximize) ctx.onMaximize();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Настройки")) { if (ctx.onOpenSettings) ctx.onOpenSettings(); }
        if (ImGui::MenuItem("О программе")) {}
        if (ImGui::MenuItem("Справка", "F1")) {}
        ImGui::Separator();
        if (ImGui::MenuItem("Закрыть", "Alt+F4")) {
            if (ctx.onClose) ctx.onClose();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // === 2. Создать ▼ ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_create", "file-plus", 20, "Создать")) {
        ImGui::OpenPopup("##create_menu");
    }
    if (ImGui::BeginPopup("##create_menu")) {
        if (ImGui::MenuItem("Деталь", "Ctrl+N")) { if (ctx.onNewDocument) ctx.onNewDocument(); }
        if (ImGui::MenuItem("Сборка", "")) {
            if (ctx.onNotImplemented) ctx.onNotImplemented("Сборка");
        }
        if (ImGui::MenuItem("Черчение", "", false, false)) {
            if (ctx.onNotImplemented) ctx.onNotImplemented("Черчение");
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // === 3. Документ (Backstage) ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_doc", "file-text", 20, "Документ")) {
        if (ctx.onGoHome) ctx.onGoHome();
    }
    ctx.tooltip->Draw("qat.doc",
                        "Документ",
                        "Backstage — все операции с файлами",
                        "Открывает экран Backstage: создание, открытие, сохранение, "
                        "экспорт, последние файлы, настройки.");

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 3. Сохранить ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_save", "save", 20, "Сохранить (Ctrl+S)")) { if (ctx.onSave) ctx.onSave(); }
    ctx.tooltip->Draw("qat.save",
                        "Сохранить (Ctrl+S)",
                        "Сохранить текущий проект в файл",
                        "Записывает все изменения текущего проекта в файл .cbf. "
                        "Если проект ещё не имеет имени — откроется диалог выбора пути.");

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 4. Undo / Redo ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_undo", "undo-2", 18, "Отменить (Ctrl+Z)")) { if (ctx.onUndo) ctx.onUndo(); }
    ImGui::SameLine();
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_redo", "redo-2", 18, "Повторить (Ctrl+Y)")) { if (ctx.onRedo) ctx.onRedo(); }

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 5. Home ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_home", "house", 20, "Главная")) {
        if (ctx.onGoHome) ctx.onGoHome();
    }

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 6. Модули ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_modules", "puzzle", 20, "Модули")) {
        // TODO: диалог управления модулями
    }
    ctx.tooltip->Draw("qat.modules",
                        "Модули",
                        "Установка и удаление модулей BreezeCAD",
                        "Управление модулями: Сборка, Черчение, Стандартные изделия, "
                        "Листовой металл. Установленные модули добавляют вкладки в ribbon.");

    ImGui::SameLine();

    // === 7. Показать формат ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_format", "layout-grid", 20, "Показать формат")) {
        ImGui::OpenPopup("##format_menu");
    }
    if (ImGui::BeginPopup("##format_menu")) {
        ImGui::PushID("format_popup");
        static bool showLines = true, showDims = true, showConstraints = true;
        ImGui::MenuItem("Линии",       nullptr, &showLines);
        ImGui::MenuItem("Размеры",     nullptr, &showDims);
        ImGui::MenuItem("Ограничения", nullptr, &showConstraints);
        ImGui::PopID();
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // === 8. Текстуры ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_textures", "palette", 20, "Текстуры")) {
        ImGui::OpenPopup("##textures_menu");
    }
    if (ImGui::BeginPopup("##textures_menu")) {
        ImGui::PushID("textures_popup");
        ImGui::TextDisabled("Материалы");
        ImGui::Separator();
        if (ImGui::MenuItem("Сталь"))    {}
        if (ImGui::MenuItem("Алюминий")) {}
        if (ImGui::MenuItem("Латунь"))   {}
        if (ImGui::MenuItem("Пластик"))  {}
        ImGui::Separator();
        if (ImGui::MenuItem("Импортировать текстуру...")) {}
        ImGui::PopID();
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // === 9. Параметры (fx) ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_params", "square-function", 20, "Параметры")) {
        ImGui::OpenPopup("##params_menu");
    }
    if (ImGui::BeginPopup("##params_menu")) {
        ImGui::PushID("params_popup");
        ImGui::TextDisabled("Таблица параметров");
        ImGui::Separator();
        ImGui::Text("Пока пусто");
        ImGui::Separator();
        if (ImGui::MenuItem("Открыть в новой вкладке")) {}
        ImGui::PopID();
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // === 10. Измерение ===
    if (UI::Widgets::QatIconButton(*ctx.icons, "qat_measure", "ruler", 20, "Измерение")) {
        ImGui::OpenPopup("##measure_menu");
    }
    if (ImGui::BeginPopup("##measure_menu")) {
        ImGui::PushID("measure_popup");
        if (ImGui::MenuItem("Расстояние"))        {}
        if (ImGui::MenuItem("Угол"))              {}
        if (ImGui::MenuItem("Радиус"))            {}
        if (ImGui::MenuItem("Диаметр"))           {}
        ImGui::Separator();
        ImGui::TextDisabled("Точность");
        static int precision = 2;
        if (ImGui::MenuItem("0.1",   nullptr, precision == 1)) precision = 1;
        if (ImGui::MenuItem("0.01",  nullptr, precision == 2)) precision = 2;
        if (ImGui::MenuItem("0.001", nullptr, precision == 3)) precision = 3;
        ImGui::PopID();
        ImGui::EndPopup();
    }

    // === 11. Правая зона: Поиск + Телеметрия ===
    ImGuiIO& io = ImGui::GetIO();
    const float searchW    = 220.0f;
    const float telemetryW = 180.0f;
    const float margin     = 12.0f;
    float rightPanelW = ctx.rightPanelExpanded ? 280.0f : 40.0f;

    float telemetryX = io.DisplaySize.x - margin - rightPanelW - telemetryW;
    float searchX    = telemetryX - searchW - 12.0f;
    float modulesX   = searchX - 32.0f - 8.0f;

    // 🧩 Модули (disabled, заглушка)
    ImGui::SameLine(modulesX);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (UI::Widgets::SvgIconBtn(*ctx.icons, "qat_modules", "puzzle", 18, 28, 26, "Модули")) {
        // TODO: меню управления модулями (Фаза 5+)
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Модули (не разработано)");

    ImGui::SameLine(searchX);
    ImGui::SetNextItemWidth(searchW);
    ImGui::InputTextWithHint("##cmd_search", "Поиск команд...",
                             ctx.searchBuffer, ctx.searchBufferSize);
    ImVec2 searchMin = ImGui::GetItemRectMin();
    ImVec2 searchMax = ImGui::GetItemRectMax();
    bool searchActive = ImGui::IsItemActive();

    // Результаты — плавающее окно
    if (ctx.searchBuffer && ctx.searchBuffer[0] != 0 && searchActive) {
        std::string query = ctx.searchBuffer;
        std::vector<std::pair<std::string, std::function<void()>>> results;
        if (ctx.commands) for (auto& cmd : *ctx.commands) {
            std::string lname = cmd.name;
            std::string lquery = query;
            for (auto& c : lname) c = (char)std::tolower((unsigned char)c);
            for (auto& c : lquery) c = (char)std::tolower((unsigned char)c);
            if (lname.find(lquery) != std::string::npos) {
                results.push_back({cmd.name + "  (" + cmd.shortcut + ")", cmd.action});
            }
        }
        ImGui::SetNextWindowPos(ImVec2(searchMin.x, searchMax.y + 4));
        ImGui::SetNextWindowSize(ImVec2(searchW + 80, 0));
        ImGui::Begin("##search_results", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);
        if (results.empty()) {
            ImGui::TextDisabled("Ничего не найдено");
        } else {
            for (auto& r : results) {
                if (ImGui::Selectable(r.first.c_str())) {
                    r.second();
                    if (ctx.searchBuffer) ctx.searchBuffer[0] = 0;
                }
            }
        }
        ImGui::End();
    }

    // Телеметрия
    ImGui::SameLine(telemetryX);
    ImGui::TextDisabled("FPS: %.0f | RAM: %.0f MB | Obj: %d",
                        io.Framerate, (double)ctx.ramMB, ctx.shapeCount);

    ImGui::PopStyleVar();
}


} // namespace BreezeCAD::UI::Qat
