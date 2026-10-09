#include "core/ViewProjection.h"

#include <cmath>
#include <utility>

namespace bz {
namespace {

std::array<double, 16> Inverse(const std::array<double, 16>& m) {
    // Gauss-Jordan on [m | I].
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c)
            a[r][c] = c < 4 ? m[static_cast<std::size_t>(r * 4 + c)] : (c - 4 == r ? 1.0 : 0.0);
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        for (int c = 0; c < 8; ++c) std::swap(a[col][c], a[pivot][c]);
        const double p = a[col][col];
        if (std::abs(p) < 1e-300) return {};
        for (int c = 0; c < 8; ++c) a[col][c] /= p;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    std::array<double, 16> out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[static_cast<std::size_t>(r * 4 + c)] = a[r][c + 4];
    return out;
}

} // namespace

Vec2 ViewProjection::Project(Vec3 p) const {
    const auto& m = matrix;
    const double x = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
    const double y = m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7];
    const double w = m[12] * p.x + m[13] * p.y + m[14] * p.z + m[15];
    const double iw = std::abs(w) > 1e-12 ? 1.0 / w : 1.0;
    return {(x * iw + 1.0) * 0.5 * width, (1.0 - y * iw) * 0.5 * height};
}

void ViewProjection::Unproject(Vec2 pixel, Vec3& origin, Vec3& dir) const {
    const std::array<double, 16> inv = Inverse(matrix);
    const double nx = pixel.x / width * 2.0 - 1.0;
    const double ny = 1.0 - pixel.y / height * 2.0;
    const auto apply = [&](double z) {
        const double x = inv[0] * nx + inv[1] * ny + inv[2] * z + inv[3];
        const double y = inv[4] * nx + inv[5] * ny + inv[6] * z + inv[7];
        const double zz = inv[8] * nx + inv[9] * ny + inv[10] * z + inv[11];
        const double w = inv[12] * nx + inv[13] * ny + inv[14] * z + inv[15];
        return Vec3{x / w, y / w, zz / w};
    };
    const Vec3 nearP = apply(-1.0);
    const Vec3 farP = apply(1.0);
    origin = nearP;
    dir = (farP - nearP).Normalized();
}

} // namespace bz
