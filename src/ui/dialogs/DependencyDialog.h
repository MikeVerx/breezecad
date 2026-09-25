#pragma once
// ============================================================
// DependencyDialog — универсальное окно зависимостей/сопряжений.
//
// СТАТУС: prototype, пока не вызывается из UI.
//   - Для 2D-ограничений эскиза избыточно (стандарт Inventor —
//     клик по кнопке + выбор геометрии, без окна).
//   - ПРИГОДИТСЯ в Фазе 4 для 3D-зависимостей сборки
//     (Mate / Parallel / Angle / Tangent) — это ровно тот паттерн.
//
// Плавающее окно: список базовых объектов, список зависимых,
// кнопки Отмена / Применить / Готово. Клики по геометрии в
// сцене добавляют объекты в списки (это делает Application).
// ============================================================

#include <functional>
#include <vector>
#include <TDF_Label.hxx>
#include "imgui.h"

namespace BreezeCAD { class OcctView; }

namespace BreezeCAD::UI::Dialogs {

struct DependencyState {
    bool open = false;
    int  type = 0;                                  // 0..7
    std::vector<TDF_Label> base;                    // базовые объекты
    std::vector<TDF_Label> dependent;               // зависимые объекты
    ImVec2 pos = ImVec2(0, 0);
    bool   posSet = false;
};

struct DependencyContext {
    DependencyState* state = nullptr;
    OcctView*        view  = nullptr;
    std::function<void(const char* title)> onApplied;   // лог применения
};

void DrawDependencyDialog(const DependencyContext& ctx);

} // namespace BreezeCAD::UI::Dialogs
