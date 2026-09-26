#pragma once

#include "ui/ribbon/RibbonState.h"

namespace BreezeCAD { class IconManager; }

#include <functional>

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

// Контекст для UI::Ribbon::Draw().
// Application собирает его каждый кадр и передаёт в модуль ribbon.
// Колбэки — std::function, чтобы модуль не знал про Application.
struct RibbonContext {
    RibbonState* state = nullptr;
    IconManager* icons = nullptr;

    // Рисование QAT (пока живёт в Application).
    std::function<void()> drawQat;

    // Рисование строки вкладок (DrawRibbonTabs).
    std::function<void()> drawTabs;

    // Рисование ещё-не-вынесенных вкладок (переходный период 10.5-10.7).
    // Model и View уже в модулях, зовутся Ribbon.cpp напрямую.
    std::function<void()> drawSketchTab;
    std::function<void()> drawAssemblyTab;
};

}}}
