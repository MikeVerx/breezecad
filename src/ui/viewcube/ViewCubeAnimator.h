#pragma once
// ============================================================
// ViewCubeAnimator — плавные повороты камеры 0.5 сек (smoothstep).
// Хранит очередь команд: два быстрых клика дают ровно 180°, а не "90 + чуть-чуть".
//
// Реализация — на шаге 1.2 рефакторинга.
// ============================================================

#include <vector>

namespace BreezeCAD::UI::ViewCube {

/// Целевая анимация. kind=0 — абсолютная (dir/up), kind=1 — roll на угол.
struct AnimTarget {
    int    kind;
    double dir[3];
    double up[3];
};

class Animator {
public:
    Animator()  = default;
    ~Animator() = default;

    // ==== Состояние (пока public — на шаге 1.2b, потом закроем) ====
    bool    myActive = false;
    float   myT      = 0.0f;
    double  myFromDir[3] = {0, 0, -1};
    double  myToDir[3]   = {0, 0, -1};
    double  myFromUp[3]  = {0, 1, 0};
    double  myToUp[3]    = {0, 1, 0};
    std::vector<AnimTarget> myQueue;

    // TODO(step 1.3/1.4): методы Update, StartAbsolute, StartRoll.
};

} // namespace BreezeCAD::UI::ViewCube
