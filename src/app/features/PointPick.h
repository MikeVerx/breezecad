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
/// a click puts a point into slot `active` (the field chosen in the panel,
/// see PointSlots): replacing it, or adding the next one. A click on empty
/// space changes nothing. The three lists stay parallel (see
/// json_io::SavePointPicks). True when they changed.
bool PickPoints(AppContext& app, Session& session, const ViewportFrame& vf, std::size_t featureIndex, std::size_t max,
                std::size_t& active, std::vector<Vec3>& points, std::vector<std::optional<doc::EdgeReference>>& edges,
                std::vector<ObjectId>& workPoints);

/// The panel fields "Точка 1: вершина", "Точка 2: ожидает выбора…" of the
/// same lists: a click on a field makes it `active`, its cross removes that
/// point. True when the lists changed.
bool PointSlots(AppContext& app, std::size_t max, std::size_t& active, std::vector<Vec3>& points,
                std::vector<std::optional<doc::EdgeReference>>& edges, std::vector<ObjectId>& workPoints, float width);

} // namespace bz::app
