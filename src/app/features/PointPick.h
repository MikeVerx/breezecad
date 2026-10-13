#pragma once
// Picking a few points in the viewport for a plane through three points or
// an axis through two: a work point under the cursor, otherwise a vertex of
// the body as it was before the feature.

#include "app/AppContext.h"
#include "document/EdgeReference.h"

#include <optional>
#include <vector>

namespace bz::app {

/// Draws the picked points numbered and the work points before the feature;
/// a click adds a point (one more than `max` starts over). The three lists
/// stay parallel (see json_io::SavePointPicks). True when they changed.
bool PickPoints(AppContext& app, Session& session, const ViewportFrame& vf, std::size_t featureIndex, std::size_t max,
                std::vector<Vec3>& points, std::vector<std::optional<doc::EdgeReference>>& edges, std::vector<ObjectId>& workPoints);

} // namespace bz::app
