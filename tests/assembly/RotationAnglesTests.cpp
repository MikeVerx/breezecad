#include "assembly/Placement.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::assembly;

namespace {

bool Same(const Rotation& a, const Rotation& b, double tol = 1e-9) {
    for (std::size_t i = 0; i < 3; ++i)
        if ((a.col[i] - b.col[i]).Length() > tol) return false;
    return true;
}

constexpr double kDeg = kPi / 180;

} // namespace

// 0.2.5: the component panel had only X/Y/Z shifts and «повернуть на 90°»;
// now the turn is typed as three angles, about the assembly's X, then Y,
// then Z (as Inventor and KOMPAS show them).
TEST_CASE("положение компонента: углы поворота вокруг X, Y, Z") {
    SUBCASE("без поворота — нули") {
        const Vec3 a = Rotation{}.AnglesXYZ();
        CHECK(a.Length() < 1e-12);
    }
    SUBCASE("один угол — поворот вокруг своей оси") {
        CHECK(Same(Rotation::FromAnglesXYZ({30 * kDeg, 0, 0}), Rotation::AxisAngle({1, 0, 0}, 30 * kDeg)));
        CHECK(Same(Rotation::FromAnglesXYZ({0, 30 * kDeg, 0}), Rotation::AxisAngle({0, 1, 0}, 30 * kDeg)));
        CHECK(Same(Rotation::FromAnglesXYZ({0, 0, 30 * kDeg}), Rotation::AxisAngle({0, 0, 1}, 30 * kDeg)));
    }
    SUBCASE("порядок: сначала X, затем Y, затем Z вокруг осей сборки") {
        const Rotation r = Rotation::FromAnglesXYZ({90 * kDeg, 90 * kDeg, 0});
        // The part's Z turned about X to -Y, then about Y it stays -Y.
        CHECK((r * Vec3{0, 0, 1} - Vec3{0, -1, 0}).Length() < 1e-9);
        // The part's X: about X stays X, about Y goes to -Z.
        CHECK((r * Vec3{1, 0, 0} - Vec3{0, 0, -1}).Length() < 1e-9);
    }
    SUBCASE("углы → поворот → те же углы") {
        for (const Vec3 deg : {Vec3{10, 20, 30}, Vec3{-45, 60, 170}, Vec3{120, -30, -90}, Vec3{0, 0, 180}}) {
            const Vec3 back = Rotation::FromAnglesXYZ(deg * kDeg).AnglesXYZ() * (1 / kDeg);
            CHECK(back.x == doctest::Approx(deg.x));
            CHECK(back.y == doctest::Approx(deg.y));
            CHECK(back.z == doctest::Approx(deg.z));
        }
    }
    SUBCASE("на «замке» (Y = ±90°) поворот всё равно восстанавливается") {
        for (const double y : {90.0, -90.0}) {
            const Rotation r = Rotation::FromAnglesXYZ(Vec3{25, y, 40} * kDeg);
            const Vec3 a = r.AnglesXYZ();
            CHECK(a.y * (1 / kDeg) == doctest::Approx(y));
            CHECK(Same(Rotation::FromAnglesXYZ(a), r, 1e-7));
        }
    }
    SUBCASE("перевёрнутая деталь — 180°, а не −180° (так и показано в панели)") {
        const Vec3 a = Rotation::AxisAngle({1, 0, 0}, kPi).AnglesXYZ() * (1 / kDeg);
        CHECK(a.x == doctest::Approx(180));
        const Vec3 z = Rotation::AxisAngle({0, 0, 1}, -kPi).AnglesXYZ() * (1 / kDeg);
        CHECK(z.z == doctest::Approx(180));
    }
    SUBCASE("поворот после трёх «на 90°» читается углами и собирается обратно") {
        Rotation r = Rotation::AxisAngle({1, 0, 0}, kPi / 2);
        r = Rotation::AxisAngle({0, 0, 1}, kPi / 2) * r;
        r = Rotation::AxisAngle({0, 1, 0}, kPi / 2) * r;
        CHECK(Same(Rotation::FromAnglesXYZ(r.AnglesXYZ()), r, 1e-9));
    }
}
