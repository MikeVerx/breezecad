#pragma once

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

// Состояние ribbon: активная вкладка, индексы инструментов, layout.
// Вынесено из Application.h (подшаг 10.1).
//
// Поля без my-префикса — это public data-holder, а не класс с инкапсуляцией.
struct RibbonState {
    int   activeTab   = 0;      // 0=3D Model, 1=Sketch, 2=Assembly, 3=View
    float groupStartY = 0.0f;   // Y-координата начала текущей группы

    // Индексы выбранного инструмента в split-кнопках (для выпадашек)
    int lineToolIndex      = 0;
    int rectToolIndex      = 0;
    int circleToolIndex    = 0;
    int arcToolIndex       = 0;
    int polyToolIndex      = 0;   // REVIEW (B-16): пока не используется
    int trimToolIndex      = 0;
    int offsetToolIndex    = 0;
    int moveToolIndex      = 0;
    int constrainToolIndex = 0;   // REVIEW (B-16): пока не используется

    // Отображение (вкладка «Вид»)
    int displayStyleIndex  = 0;   // 7 вариантов
    int backgroundIndex    = 0;   // 5 вариантов
};

}}}
