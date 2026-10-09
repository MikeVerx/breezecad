#pragma once
// Planes drawn over the 3D view: the origin planes and the work planes of
// the part. They are drawn by the UI (not OCCT) as squares around the part,
// so they can be hovered and clicked like the rest of the overlays.

#include "app/AppContext.h"

#include <optional>
#include <string>
#include <vector>

namespace bz::app {

struct ShownPlane {
    ObjectId    id;        ///< origin plane or work plane feature
    Frame       frame;
    Vec2        centre;    ///< middle of the square, plane coordinates
    double      half = 40.0;
    std::string label;     ///< "XY", "Плоскость1"
};

/// The three origin planes, sized to the part.
std::vector<ShownPlane> OriginPlanes(const Session& session);
/// Active, placed work planes of the part, before index `before` (all by default).
std::vector<ShownPlane> WorkPlanes(const Session& session, std::size_t before = static_cast<std::size_t>(-1));

/// Distance along the cursor ray to the plane's square, nullopt when missed.
std::optional<double> HitPlane(const ViewportFrame& vf, ImVec2 mouse, const ShownPlane& plane);
/// The plane under the cursor nearest the eye, or nullptr.
const ShownPlane* PlaneUnderCursor(const ViewportFrame& vf, ImVec2 mouse, const std::vector<ShownPlane>& planes);

/// Draws the placed work axes before index `before` (all by default): a long
/// dash-dot line through the part with its name. `hot` is drawn stronger.
void DrawWorkAxes(AppContext& app, ImDrawList* dl, const ViewportFrame& vf, const Session& session,
                  std::size_t before = static_cast<std::size_t>(-1), ObjectId hot = {});

/// Queues the plane for this frame's 3D view (DrawViewport hands the queue
/// to the viewport) and draws its label. `hot`: hovered or chosen. `faint`:
/// stepped back so a body stays readable.
void DrawPlane(AppContext& app, ImDrawList* dl, const ViewportFrame& vf, const ShownPlane& plane, bool hot, bool faint = false);

} // namespace bz::app
