#pragma once
// ============================================================
// ViewCubeOverlay — 3D-кубик навигации.
//
// Публичный API: Draw(ctx). Внутри: грани, рёбра, углы, подписи,
// hover, анимация камеры, кнопки << >>.
//
// Зависимости инжектируются через Context — никаких синглтонов.
// Слои: ui зависит от ImGui и kernel через интерфейсы, не наоборот.
//
// Реализация — на шаге 1.4 рефакторинга.
// ============================================================

#include "ViewCubeAnimator.h"
#include "imgui.h"

// Forward decls — избегаем тяжёлых заголовков в публичном API
namespace BreezeCAD {
class OcctView;
struct ThemeTokens;
}

namespace BreezeCAD::UI::ViewCube {

class ViewCubeOverlay {
public:
    /// Всё, что нужно для отрисовки — без глобального состояния.
    struct Context {
        OcctView&            view;
        const ThemeTokens&   theme;
        ImGuiIO&             io;
        float                rightPanelShiftX;  // 55 или 110
    };

    ViewCubeOverlay()  = default;
    ~ViewCubeOverlay() = default;

    /// Отрисовать кубик, обработать hover / клики по граням, рёбрам и углам.
    /// Плавно анимирует камеру (0.5 с, smoothstep, очередь поворотов).
    void Draw(const Context& ctx);

private:
    Animator myAnimator;
    float    myButtonsTimer = 0.0f;
};

} // namespace BreezeCAD::UI::ViewCube
