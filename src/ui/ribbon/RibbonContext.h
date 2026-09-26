#pragma once

#include "ui/ribbon/RibbonState.h"
#include "doc/BreezeDocument.h"   // LineStyle

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

    // Данные для вкладки «Эскиз» (Application заполняет каждый кадр).
    bool         inSketch          = false;
    LineStyle*   currentLineStyle  = nullptr;
    bool*        hasSketchStart    = nullptr;
    std::function<void()> clearPreviewLine;
    std::function<void()> exitSketch;

    // Рисование QAT (пока живёт в Application).
    std::function<void()> drawQat;

    // Рисование строки вкладок (DrawRibbonTabs).
    std::function<void()> drawTabs;
};

}}}
