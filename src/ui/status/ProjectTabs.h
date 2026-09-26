#pragma once
// ============================================================
// ProjectTabs — нижний таб-бар проектов (52 px).
//
// Показывает список открытых документов, домашнюю кнопку, [+].
// Действия (создать/закрыть/переключиться) — через коллбеки,
// чтобы модуль не зависел от Application.
// ============================================================

#include <functional>
#include <string>
#include <vector>

namespace BreezeCAD { class IconManager; }

namespace BreezeCAD::UI::Status {

struct ProjectTabsContext {
    const std::vector<std::string>* names;       // имена проектов
    const std::vector<std::string>* paths;       // пути (пусто = не сохранён)
    int  activeIndex     = -1;
    int  documentCount   = 0;
    bool onHome          = true;
    IconManager* icons   = nullptr;

    std::function<void()>    onGoHome;
    std::function<void()>    onCreate;
    std::function<void(int)> onSwitch;
    std::function<void(int)> onClose;
};

void DrawProjectTabs(const ProjectTabsContext& ctx);

} // namespace BreezeCAD::UI::Status
