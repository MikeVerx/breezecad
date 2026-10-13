#pragma once

#include "app/AppContext.h"

namespace bz::app {

/// The "Тачпад" tab of the settings: each gesture with a moving picture and
/// what it does; the gestures this system does not deliver are greyed out.
void DrawTouchpadSettings(AppContext& app, float width);

} // namespace bz::app
