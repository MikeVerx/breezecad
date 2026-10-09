#pragma once
// Mapping between sketch coordinates (mm in the sketch plane) and the screen.

#include "app/AppContext.h"
#include "core/Math.h"

#include <optional>

namespace bz::app {

struct SketchView {
    Frame         frame;
    ViewportFrame vf;

    ImVec2 ToScreen(Vec2 p) const { return vf.Project(frame.ToWorld(p)); }

    /// Point of the sketch plane under a screen position (nullopt when the
    /// plane is seen edge-on).
    std::optional<Vec2> FromScreen(ImVec2 screen) const {
        Vec3 origin, dir;
        vf.projection.Unproject(vf.ToLocal(screen), origin, dir);
        const double denom = dir.Dot(frame.normal);
        if (std::abs(denom) < 1e-9) return std::nullopt;
        const double t = (frame.origin - origin).Dot(frame.normal) / denom;
        return frame.ToLocal(origin + dir * t);
    }

    /// Millimetres per screen pixel on the sketch plane (orthographic view).
    double MmPerPixel() const { return vf.projection.mmPerPixel; }
};

} // namespace bz::app
