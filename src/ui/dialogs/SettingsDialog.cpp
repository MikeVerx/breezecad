#include "SettingsDialog.h"

#include "imgui.h"

namespace BreezeCAD::UI::Dialogs {

void DrawSettingsDialog(const SettingsContext& ctx) {
    if (!ctx.state || !ctx.state->show) return;

    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                             ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Настройки", &ctx.state->show,
                      ImGuiWindowFlags_NoCollapse)) {

        // Левая панель — вкладки
        ImGui::BeginChild("##settings_tabs", ImVec2(180, 0), true);
        const char* tabs[] = {
            "Основные", "Графика", "Тема", "Язык", "Хоткеи", "О программе"
        };
        for (int i = 0; i < 6; i++) {
            if (ImGui::Selectable(tabs[i], ctx.state->activeTab == i)) {
                ctx.state->activeTab = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##settings_content", ImVec2(0, 0), true);

        switch (ctx.state->activeTab) {
            case 0: {
                ImGui::Text("Основные настройки");
                ImGui::Separator();
                ImGui::Spacing();

                // NOTE: пока заглушки, не влияют ни на что (B-11)
                static int units = 0;
                ImGui::Text("Единицы измерения:");
                ImGui::RadioButton("Миллиметры (мм)", &units, 0); ImGui::SameLine();
                ImGui::RadioButton("Дюймы (in)",      &units, 1);

                ImGui::Spacing();
                static int precision = 2;
                ImGui::SliderInt("Знаков после запятой", &precision, 0, 6);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Автосохранение:");
                static bool autosave = true;
                ImGui::Checkbox("Включить автосохранение", &autosave);
                static int autosaveInterval = 5;
                ImGui::SliderInt("Интервал (мин)", &autosaveInterval, 1, 30);
                break;
            }
            case 1: {
                ImGui::Text("Графика");
                ImGui::Separator();
                ImGui::TextDisabled("(будет заполнено позже)");
                ImGui::Spacing();
                static int quality = 1;
                ImGui::Text("Качество рендеринга:");
                ImGui::RadioButton("Низкое",  &quality, 0); ImGui::SameLine();
                ImGui::RadioButton("Среднее", &quality, 1); ImGui::SameLine();
                ImGui::RadioButton("Высокое", &quality, 2);
                ImGui::Spacing();
                static bool aa = true;
                ImGui::Checkbox("Сглаживание (antialiasing)", &aa);
                static bool shadows = false;
                ImGui::Checkbox("Тени", &shadows);
                break;
            }
            case 2: {
                ImGui::Text("Тема оформления");
                ImGui::Separator();
                ImGui::Spacing();

                int theme = ctx.currentTheme ? *ctx.currentTheme : 1;
                if (ImGui::RadioButton("Светлая", &theme, 0)) {
                    if (ctx.onThemeChange) ctx.onThemeChange(0);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Тёмная", &theme, 1)) {
                    if (ctx.onThemeChange) ctx.onThemeChange(1);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Авто", &theme, 2)) {
                    if (ctx.onThemeChange) ctx.onThemeChange(2);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Цвета линий эскиза:");

                // NOTE: заглушки, применение — позже (B-11)
                static ImVec4 colNormal(0.75f, 0.2f, 1.0f, 1.0f);
                ImGui::ColorEdit3("Основная (недоопределённая)", (float*)&colNormal);

                static ImVec4 colConstruction(0.55f, 0.55f, 0.75f, 1.0f);
                ImGui::ColorEdit3("Вспомогательная", (float*)&colConstruction);

                static ImVec4 colCenterline(0.9f, 0.45f, 0.15f, 1.0f);
                ImGui::ColorEdit3("Осевая", (float*)&colCenterline);

                ImGui::Spacing();
                ImGui::TextDisabled("(применение цветов — позже)");
                break;
            }
            case 3: {
                ImGui::Text("Язык интерфейса");
                ImGui::Separator();
                Language cur = Localization::Instance().GetLanguage();
                if (ImGui::RadioButton("English", cur == Language::EN))
                    if (ctx.onLanguageChange) ctx.onLanguageChange(Language::EN);
                if (ImGui::RadioButton("Русский", cur == Language::RU))
                    if (ctx.onLanguageChange) ctx.onLanguageChange(Language::RU);
                break;
            }
            case 4: {
                ImGui::Text("Горячие клавиши");
                ImGui::Separator();
                ImGui::TextDisabled("(редактор хоткеев — позже)");
                ImGui::Spacing();
                ImGui::Text("Ctrl+N  — Новый проект");
                ImGui::Text("Ctrl+O  — Открыть");
                ImGui::Text("Ctrl+S  — Сохранить");
                ImGui::Text("Ctrl+Z  — Отменить");
                ImGui::Text("Ctrl+Y  — Повторить");
                ImGui::Text("F11     — Полный экран");
                ImGui::Text("F6      — Изометрия (домашний вид)");
                ImGui::Text("Esc     — Прервать линию / выйти из эскиза");
                break;
            }
            case 5: {
                ImGui::Text("О программе BreezeCAD");
                ImGui::Separator();
                ImGui::Text("Версия: 0.1.0 (prototype)");
                ImGui::Text("Сборка: %s %s", __DATE__, __TIME__);
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "BreezeCAD — лёгкая кроссплатформенная САПР на базе "
                    "OpenCASCADE и Dear ImGui.");
                ImGui::Spacing();
                ImGui::Text("Ядро: OpenCASCADE 8.0");
                ImGui::Text("UI: Dear ImGui + GLFW");
                break;
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace BreezeCAD::UI::Dialogs
