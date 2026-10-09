#pragma once
// Design tokens from the approved mockups (light and dark). Every colour in
// the UI comes from here; UI code never writes a colour literal (RULES 30).

#include "core/Color.h"

namespace bz::ui {

struct ThemeColors {
    // surfaces, from the window background up
    Color appBackground;     ///< header, mode bar, left rail, tab bar
    Color ribbon;            ///< ribbon and active mode tab
    Color panel;             ///< left panel
    Color statusBar;
    Color field;             ///< inputs
    Color fieldBorder;
    Color segmentTrack;      ///< background of segmented controls
    Color segmentActive;     ///< the chosen segment
    Color divider;           ///< hairlines between areas
    Color dividerSoft;       ///< hairlines inside panels

    // interaction
    Color hover;
    Color hoverRow;          ///< tree row hover
    Color selectedRow;
    Color pressed;

    // text
    Color text;
    Color textSecondary;
    Color textMuted;
    Color textDisabled;
    Color textOnAccent;
    Color tooltip;           ///< tooltip background (inverted in the light theme)
    Color tooltipText;

    // brand
    Color accent;            ///< primary buttons, active tab underline, focus
    Color accentHover;
    Color accentSoft;        ///< selected rail button, chips
    Color accentSoftBorder;

    // status
    Color success;
    Color warning;
    Color danger;

    // viewport
    Color viewportTop;
    Color viewportBottom;
    Color body;
    Color bodyEdges;
    Color hoverHighlight;
    Color selection;

    // sketch overlay
    Color sketchFree;        ///< under-defined geometry
    Color sketchDefined;     ///< fully defined geometry
    Color sketchConstruction;
    Color sketchProjected;    ///< model edges projected into the sketch
    Color sketchHover;
    Color sketchSelected;
    Color sketchPoint;
    Color sketchGrid;
    Color sketchGridMajor;
    Color dimension;
    Color constraintGlyph;
    Color regionFill;        ///< profile region under the cursor / picked
    Color planeFill;         ///< origin planes while choosing a sketch plane
    Color planeEdge;
    Color axisX, axisY, axisZ;

    // view cube and its buttons
    Color cubeLight;         ///< face colour toward the light
    Color cubeShade;         ///< face colour away from the light
    Color cubeHover;
    Color cubeStroke;
    Color cubeInner;         ///< bevel lines
    Color cubeText;
    Color floatingButton;    ///< buttons floating over the viewport
    Color floatingBorder;
    Color floatingInk;
};

/// Sizes in logical pixels at 100 % scale. Multiply by the UI scale.
struct ThemeMetrics {
    float headerHeight   = 40.0f;
    float modeBarHeight  = 34.0f;
    float ribbonHeight   = 104.0f;
    float railWidth      = 48.0f;
    float panelWidth     = 296.0f;
    float tabBarHeight   = 34.0f;
    float statusHeight   = 26.0f;
    float rowHeight      = 30.0f;
    float fieldHeight    = 36.0f;
    float radius         = 8.0f;
    float radiusSmall    = 6.0f;
};

struct Theme {
    bool         dark = false;
    ThemeColors  colors;
    ThemeMetrics metrics;

    static Theme Light();
    static Theme Dark();
};

/// Applies the theme to Dear ImGui's style (call after changing theme or scale).
void ApplyToImGui(const Theme& theme, float scale);

} // namespace bz::ui
