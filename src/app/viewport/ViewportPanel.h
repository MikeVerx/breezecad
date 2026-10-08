#pragma once

#include "app/AppContext.h"

namespace bz::app {

/// The 3D scene: OCCT image, KOMPAS-style navigation, the overlays of the
/// current mode (sketch, plane picking, extrusion profiles), view cube,
/// context menu.
void DrawViewport(AppContext& app, Session& session, ImVec2 pos, ImVec2 size);

} // namespace bz::app
