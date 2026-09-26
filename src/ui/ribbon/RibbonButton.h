#pragma once

namespace BreezeCAD {
class IconManager;

namespace UI {
namespace Ribbon {

// Универсальная кнопка ribbon: SVG-иконка + подпись + опциональная стрелка.
// opts — массив ключей локализации, idx — индекс выбранного.
// Возвращает: 0 = не нажато, 1 = основная, 2 = стрелка ▼, 3 = выбор в popup.
int Button(IconManager& icons,
           const char* id, const char* const* opts, int optCount, int& idx,
           const char* icon, bool enabled, bool big,
           const char* tooltip = nullptr, float iconScale = 1.0f);

// Кнопка без выпадашки.
int SingleButton(IconManager& icons,
                 const char* id, const char* key, const char* icon,
                 bool enabled, bool big,
                 const char* tooltip = nullptr, float iconScale = 1.0f);

}}}
