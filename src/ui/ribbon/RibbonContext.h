#pragma once

#include "ui/ribbon/RibbonState.h"

#include <functional>

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

// Контекст для UI::Ribbon::Draw().
// Application собирает его каждый кадр и передаёт в модуль ribbon.
// Колбэки — std::function, чтобы модуль не знал про Application.
struct RibbonContext {
    RibbonState* state = nullptr;

    // Рисование QAT (пока живёт в Application).
    std::function<void()> drawQat;

    // Рисование строки вкладок (DrawRibbonTabs).
    std::function<void()> drawTabs;

    // Рисование содержимого активной вкладки.
    std::function<void()> drawContent;
};

}}}
