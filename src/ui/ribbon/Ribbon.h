#pragma once

#include "ui/ribbon/RibbonContext.h"

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

// Рисует окно ribbon: QAT + вкладки + содержимое.
// Вызывается из Application::DrawRibbon() каждый кадр.
void Draw(RibbonContext& ctx);

}}}
