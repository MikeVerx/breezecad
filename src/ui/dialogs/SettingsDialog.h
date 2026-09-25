#pragma once
// ============================================================
// SettingsDialog — окно настроек (720x480).
//
// 6 вкладок: Основные / Графика / Тема / Язык / Хоткеи /
// О программе.
//
// РАБОТАЕТ СЕЙЧАС: тема, язык.
// ОСТАЛЬНОЕ — визуальные заглушки (единицы, точность, автосейв,
// качество, AA, тени, цвета линий эскиза). См. REFACTOR_NOTES B-11.
// ============================================================

#include <functional>
#include "app/Localization.h"      // Language

namespace BreezeCAD::UI::Dialogs {

struct SettingsState {
    bool show = false;
    int  activeTab = 0;
};

struct SettingsContext {
    SettingsState* state = nullptr;
    int* currentTheme = nullptr;   // rw
    std::function<void(int theme)>              onThemeChange;
    std::function<void(Language lang)>          onLanguageChange;
};

void DrawSettingsDialog(const SettingsContext& ctx);

} // namespace BreezeCAD::UI::Dialogs
