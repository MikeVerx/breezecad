#pragma once

#include "ui/ribbon/RibbonState.h"

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

// Начало группы. Запоминает Y-координату старта в state.
void GroupStart(RibbonState& state);

// Конец группы. Рисует фон, разделитель, заголовок снизу.
void GroupEnd(const char* name, RibbonState& state);

}}}
