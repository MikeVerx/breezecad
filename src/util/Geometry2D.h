#pragma once
// ============================================================
// Geometry2D — чистая математика в плоскости эскиза.
//
// Работает с gp_Pln (плоскость эскиза) и её gp_Ax3.
// Все функции — без состояния, без побочных эффектов.
//
// Используется для:
//   - длины и угла между точками (2D-проекция)
//   - орто-выравнивания (угол кратен 90°)
//   - привязки к сетке 10 мм
//   - построения точки по длине и углу
// ============================================================

#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

namespace BreezeCAD::Util {

/// Длина и угол (в градусах) вектора from->to в плоскости.
void ComputeLengthAngle(const gp_Pln& plane,
                        const gp_Pnt& from,
                        const gp_Pnt& to,
                        double& length,
                        double& angleDeg);

/// Выровнять точку to относительно from по ближайшей оси (0/90/180/270).
gp_Pnt ApplyOrtho(const gp_Pln& plane,
                  const gp_Pnt& from,
                  const gp_Pnt& to);

/// Привязать точку к сетке 10 мм в плоскости эскиза.
gp_Pnt SnapToGridPoint(const gp_Pnt& p, const gp_Pln& plane);

/// Построить точку от from по длине и углу (в градусах).
gp_Pnt PointFromLengthAngle(const gp_Pln& plane,
                            const gp_Pnt& from,
                            double length,
                            double angleDeg);

} // namespace BreezeCAD::Util
