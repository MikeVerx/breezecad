#pragma once
// ============================================================
// SelectionRect — отрисовка прямоугольника выделения поверх
// 3D-сцены. Данные о прямоугольнике берёт из OcctView.
//
// Функция Draw вызывается каждый кадр, если OcctView активен
// в режиме прямоугольного выделения.
// ============================================================

namespace BreezeCAD { class OcctView; }
class GLFWwindow;

namespace BreezeCAD::UI::Widgets {

/// Нарисовать прямоугольник выделения, если он активен.
void DrawSelectionRect(OcctView& view, GLFWwindow* window);

} // namespace BreezeCAD::UI::Widgets
