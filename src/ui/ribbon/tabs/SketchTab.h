#pragma once

#include "doc/BreezeDocument.h"   // LineStyle

#include <functional>

namespace BreezeCAD {
class IconManager;

namespace UI {
namespace Ribbon {

struct RibbonState;

// Контекст вкладки «Эскиз». Application собирает его каждый кадр.
struct SketchTabContext {
    IconManager* icons   = nullptr;
    RibbonState* state   = nullptr;
    bool         inSketch = false;

    // Состояние рисования (указатели на поля Application — чтобы менять).
    LineStyle* currentLineStyle = nullptr;
    bool*      hasSketchStart   = nullptr;

    // Колбэки.
    std::function<void()> clearPreviewLine;
    std::function<void()> exitSketch;
};

// Вкладка «Эскиз».
void DrawSketchTab(SketchTabContext& ctx);

}}}
