#pragma once

#include "core/Math.h"

#include <array>

namespace bz {

/// World -> screen mapping of the current frame, for overlays.
struct ViewProjection {
    std::array<double, 16> matrix{};   ///< row-major projection * view
    double width  = 1.0;               ///< viewport size, logical pixels
    double height = 1.0;
    Vec3   direction{0, 0, -1};        ///< view direction (from eye to center)
    Vec3   up{0, 0, 1};
    double mmPerPixel = 1.0;           ///< model size of one pixel (orthographic)

    /// Screen position (logical pixels, origin top-left) of a model point.
    Vec2 Project(Vec3 p) const;
    /// Ray through a screen point: origin on the near plane and direction.
    void Unproject(Vec2 pixel, Vec3& origin, Vec3& dir) const;
};

} // namespace bz
