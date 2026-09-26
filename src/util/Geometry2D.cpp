#include "Geometry2D.h"

#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#include <cmath>

namespace BreezeCAD::Util {

void ComputeLengthAngle(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to,
                        double& length, double& angleDeg) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    length = std::sqrt(u*u + w*w);
    angleDeg = std::atan2(w, u) * 180.0 / M_PI;
}

gp_Pnt ApplyOrtho(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    double len = std::sqrt(u*u + w*w);
    if (len < 1e-6) return to;

    // Выбираем ближайшее направление: 0 / 90 / 180 / 270
    double u2 = 0.0, w2 = 0.0;
    if (std::abs(u) >= std::abs(w)) {
        u2 = (u >= 0) ? len : -len;
        w2 = 0.0;
    } else {
        u2 = 0.0;
        w2 = (w >= 0) ? len : -len;
    }

    gp_Pnt result = from;  // орто работает от стартовой точки
    result.Translate(gp_Vec(xDir) * u2);
    result.Translate(gp_Vec(yDir) * w2);
    return result;
}

gp_Pnt SnapToGridPoint(const gp_Pnt& p, const gp_Pln& plane) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir   = ax.XDirection();
    gp_Dir yDir   = ax.YDirection();

    gp_Vec v(origin, p);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    const double step = 10.0;
    u = std::round(u / step) * step;
    w = std::round(w / step) * step;

    gp_Pnt snapped = origin;
    snapped.Translate(gp_Vec(xDir) * u);
    snapped.Translate(gp_Vec(yDir) * w);
    return snapped;
}

gp_Pnt PointFromLengthAngle(const gp_Pln& plane,
                            const gp_Pnt& from,
                            double length, double angleDeg) {
    gp_Ax3 ax = plane.Position();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    double rad = angleDeg * M_PI / 180.0;
    double u = length * std::cos(rad);
    double w = length * std::sin(rad);

    gp_Pnt result = from;
    result.Translate(gp_Vec(xDir) * u);
    result.Translate(gp_Vec(yDir) * w);
    return result;
}

} // namespace BreezeCAD::Util
