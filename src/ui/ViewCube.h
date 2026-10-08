#pragma once
// Inventor-style navigation cube with face, edge and corner regions, the
// 90-degree arrows, roll buttons, the home button and the axis triad.

#include "core/Math.h"
#include "core/ViewProjection.h"
#include "ui/Ui.h"

#include <optional>

namespace bz::ui {

/// What the user asked the cube for this frame.
struct ViewCubeAction {
    /// Look at the model from this direction (unit vector from the model
    /// toward the camera) with this up vector.
    std::optional<std::pair<Vec3, Vec3>> lookFrom;
    bool   home      = false;   ///< back to the isometric home view
    double dragYaw   = 0.0;     ///< orbit by dragging the cube, radians
    double dragPitch = 0.0;
};

/// Draws the cube in the top-right corner of the viewport rectangle
/// [`topLeft`, `bottomRight`] and the triad in its bottom-left corner.
ViewCubeAction DrawViewCube(Ui& ui, const ViewProjection& view, ImVec2 topLeft, ImVec2 bottomRight);

/// Home view direction (from the model toward the camera): front-right-top.
inline constexpr Vec3 kHomeDirection{1.0, -1.0, 1.0};

} // namespace bz::ui
