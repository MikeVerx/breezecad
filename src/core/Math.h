#pragma once

#include <cmath>

namespace bz {

inline constexpr double kPi = 3.14159265358979323846;

/// Plain 2D vector for sketch geometry. Kernel-agnostic on purpose.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr bool operator==(const Vec2&) const = default;

    constexpr double Dot(Vec2 o) const { return x * o.x + y * o.y; }
    constexpr double Cross(Vec2 o) const { return x * o.y - y * o.x; }
    double Length() const { return std::hypot(x, y); }
    Vec2 Normalized() const {
        const double len = Length();
        return len > 0.0 ? *this / len : Vec2{};
    }
    constexpr Vec2 Perp() const { return {-y, x}; }
};

/// Plain 3D vector. Used for planes and camera data outside the kernel.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr bool operator==(const Vec3&) const = default;

    constexpr double Dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vec3 Cross(Vec3 o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double Length() const { return std::sqrt(Dot(*this)); }
    Vec3 Normalized() const {
        const double len = Length();
        return len > 0.0 ? *this * (1.0 / len) : Vec3{};
    }
};

/// Right-handed orthonormal frame: a sketch plane or a workplane.
/// xDir and normal are unit and perpendicular; yDir = normal x xDir.
struct Frame {
    Vec3 origin{0, 0, 0};
    Vec3 xDir{1, 0, 0};
    Vec3 normal{0, 0, 1};

    Vec3 YDir() const { return normal.Cross(xDir); }
    Vec3 ToWorld(Vec2 p) const { return origin + xDir * p.x + YDir() * p.y; }
    Vec2 ToLocal(Vec3 p) const {
        const Vec3 d = p - origin;
        return {d.Dot(xDir), d.Dot(YDir())};
    }
};

} // namespace bz
