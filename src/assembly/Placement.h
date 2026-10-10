#pragma once
// Where a component sits in its assembly: a rotation and a shift that take
// a point of the part (its own coordinates) to the assembly's coordinates.

#include "core/Math.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace bz::assembly {

/// 3x3 rotation; columns are the part's X, Y, Z axes seen in the assembly.
struct Rotation {
    std::array<Vec3, 3> col{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};

    Vec3 operator*(Vec3 v) const { return col[0] * v.x + col[1] * v.y + col[2] * v.z; }
    Rotation operator*(const Rotation& o) const {
        return Rotation{{*this * o.col[0], *this * o.col[1], *this * o.col[2]}};
    }
    Rotation Transposed() const {
        return Rotation{{Vec3{col[0].x, col[1].x, col[2].x}, Vec3{col[0].y, col[1].y, col[2].y},
                         Vec3{col[0].z, col[1].z, col[2].z}}};
    }

    /// Rotation by |v| radians about v (the exponential map; zero = none).
    static Rotation FromVector(Vec3 v) {
        const double angle = v.Length();
        if (angle < 1e-15) return {};
        return AxisAngle(v * (1.0 / angle), angle);
    }
    static Rotation AxisAngle(Vec3 axis, double angle) {
        const Vec3   k = axis.Normalized();
        const double c = std::cos(angle), s = std::sin(angle), t = 1.0 - c;
        auto column = [&](Vec3 e) {   // Rodrigues: e c + (k x e) s + k (k.e) t
            return e * c + k.Cross(e) * s + k * (k.Dot(e) * t);
        };
        return Rotation{{column({1, 0, 0}), column({0, 1, 0}), column({0, 0, 1})}};
    }
    /// From the part's X and Z axes (made orthonormal, Z kept).
    static Rotation FromXZ(Vec3 x, Vec3 z) {
        const Vec3 zn = z.Normalized();
        Vec3 xn = (x - zn * x.Dot(zn)).Normalized();
        if (xn.Length() < 0.5) xn = (std::abs(zn.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
        xn = (xn - zn * xn.Dot(zn)).Normalized();
        return Rotation{{xn, zn.Cross(xn), zn}};
    }
    /// Re-orthonormalizes after many small steps.
    Rotation Orthonormalized() const { return FromXZ(col[0], col[2]); }
};

struct Placement {
    Rotation rotation;
    Vec3     translation;

    Vec3 Point(Vec3 local) const { return rotation * local + translation; }
    Vec3 Direction(Vec3 local) const { return rotation * local; }
    Placement Inverse() const {
        const Rotation rt = rotation.Transposed();
        return {rt, -(rt * translation)};
    }
    /// This placement after `inner`: Point(inner.Point(p)).
    Placement operator*(const Placement& inner) const {
        return {rotation * inner.rotation, rotation * inner.translation + translation};
    }
    bool IsIdentity(double tol = 1e-12) const {
        const Rotation id;
        for (std::size_t i = 0; i < 3; ++i)
            if ((rotation.col[i] - id.col[i]).Length() > tol) return false;
        return translation.Length() <= tol;
    }
};

} // namespace bz::assembly
