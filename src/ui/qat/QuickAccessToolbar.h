#pragma once
// ============================================================
// QuickAccessToolbar — верхняя полоса QAT.
//
// 11 кнопок слева + поиск команд + телеметрия справа.
// Не зависит от Application: получает QatContext с данными
// и коллбеками на действия.
// ============================================================

#include <functional>
#include <string>
#include <vector>

namespace BreezeCAD { class IconManager; }
namespace BreezeCAD::UI::Widgets { class TooltipState; }

namespace BreezeCAD::UI::Qat {

/// Одна команда для поиска. Поля совпадают с Application::CommandEntry.
struct QatCommand {
    std::string id;
    std::string name;
    std::string shortcut;
    std::function<void()> action;
};

struct QatContext {
    // === Данные для отрисовки ===
    IconManager*                icons = nullptr;
    UI::Widgets::TooltipState*  tooltip = nullptr;
    char*                       searchBuffer = nullptr;   // буфер 128 байт
    int                         searchBufferSize = 128;
    const std::vector<QatCommand>* commands = nullptr;

    float  ramMB = 0.0f;
    int    shapeCount = 0;
    bool   rightPanelExpanded = false;

    // === Коллбеки на действия ===
    std::function<void()> onNewDocument;
    std::function<void()> onSave;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onGoHome;          // «Документ» / «Главная» — сохранить превью + home
    std::function<void()> onOpenSettings;
    std::function<void(const char*)> onNotImplemented;   // Сборка / Черчение

    // Действия окна
    std::function<void()> onMinimize;
    std::function<void()> onMaximize;
    std::function<void()> onClose;
};

void DrawQuickAccessToolbar(const QatContext& ctx);

} // namespace BreezeCAD::UI::Qat
