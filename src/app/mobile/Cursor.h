#pragma once
// The drop cursor and the magnifier at its tip (docs/ADR/0004-mobile-touch-ui.md,
// "Курсор-капля", "Точность").
//
// A round part under the finger and a tip a little above it, marked in a
// contrasting colour: the finger never covers the point it works on. The
// magnifier shows the area around the tip, enlarged, away from the finger
// (a sketch only): what is under the tip is seen before "OK".

#include "app/AppContext.h"

namespace bz::app::mobile {

/// Draws the cursor in the view (its draw list), when it is in use.
void DrawDropCursor(AppContext& app, ImDrawList* dl);

/// The magnifier: what the screen showed around the cursor's tip in the
/// last frame, copied there by CaptureMagnifier.
class Magnifier {
public:
    ~Magnifier();
    /// Draws it this frame if it is wanted (a sketch, the cursor led by a finger).
    void Draw(AppContext& app);
    /// After the frame is drawn, before it is shown: copies the area around
    /// the tip from the window (pixels = dp × `density`, `windowHeight` in pixels).
    void Capture(const AppContext& app, float density, int windowHeight);

private:
    bool Wanted(const AppContext& app) const;
    unsigned myTexture = 0;
    int      mySize = 0;        ///< pixels, square
    bool     myHas = false;     ///< a capture of the last frame is in the texture
};

} // namespace bz::app::mobile
