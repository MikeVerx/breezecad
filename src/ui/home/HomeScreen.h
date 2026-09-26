#pragma once
// ============================================================
// HomeScreen — стартовая страница.
//
// Титул + топ-полоса (Деталь/Сборка/Открыть) + левая колонка
// (лого, версия, кнопки) + правая область (плитки/список).
//
// Не зависит от Application: получает HomeContext со ссылками
// на данные и коллбеками. Состояние home (viewMode, search)
// передаётся через указатели.
// ============================================================

#include <functional>
#include <string>
#include <vector>

namespace BreezeCAD {
class IconManager;
struct ThemeTokens;
}

namespace BreezeCAD::UI::Home {

struct HomeContext {
    // === Данные (read-only) ===
    const std::vector<std::string>* projectNames = nullptr;
    const std::vector<std::string>* projectPaths = nullptr;
    const std::vector<std::string>* thumbnails   = nullptr;
    int documentCount = 0;

    // === Состояние (rw) ===
    bool* showHomeScreen = nullptr;
    int*  viewMode       = nullptr;   // 0 = плитки, 1 = список
    char* searchBuffer   = nullptr;
    int   searchBufferSize = 128;

    // === Окружение ===
    IconManager*       icons = nullptr;
    const ThemeTokens* theme = nullptr;
    double ramMB = 0.0;

    // === Коллбеки ===
    std::function<void(int)> onSwitchDoc;
    std::function<void(int)> onCloseDoc;
    std::function<void()>    onCreateDoc;
    std::function<void()>    onOpenFile;
    std::function<void()>    onShowSettings;
    std::function<void(const char*)> onNotImplemented;
    std::function<void()>    onQuit;
};

void DrawHomeScreen(const HomeContext& ctx);

} // namespace BreezeCAD::UI::Home
