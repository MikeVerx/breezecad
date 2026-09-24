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

    // TODO(step 1.2):
    //   void Update(float dt, bool& outActive, double outDir[3], double outUp[3]);
    //   void StartAbsolute(double dx, double dy, double dz,
    //                      double ux, double uy, double uz);
    //   void StartRoll(double angleDeg,
    //                  double curDirX, double curDirY, double curDirZ,
    //                  double curUpX,  double curUpY,  double curUpZ);
    //   bool IsActive() const;

private:
    // TODO(step 1.2): активность, прогресс t, from/to, очередь
};

} // namespace BreezeCAD::UI::ViewCube
