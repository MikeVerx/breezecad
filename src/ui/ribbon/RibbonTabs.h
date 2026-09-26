#pragma once

#include "ui/ribbon/RibbonState.h"

namespace BreezeCAD {
class IconManager;

namespace UI {
namespace Ribbon {

// Рисует строку вкладок ribbon + кнопку "+".
void DrawTabs(IconManager& icons, RibbonState& state);

}}}
