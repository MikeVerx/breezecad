#pragma once
// Touch, stylus and mouse on a touch screen (docs/ADR/0004-mobile-touch-ui.md):
// where each pointer belongs, and what ImGui and the 3D view get from it.
//
// Two layers, as the ADR says: the buttons on top, the work area under
// them. A finger that lands on a panel is ImGui's mouse there (a tap
// clicks, a drag up or down scrolls, sideways drags a slider, a held press
// is a right click). A finger that lands in the view goes to the gestures
// (mobile/Gestures.h) and stays theirs over the panels until it lifts.
//
// The tools of the program only know the mouse, so everything they get is
// mouse: a tap with the cursor off is a click where the finger was; with the
// cursor on, the mouse is the cursor's tip and "OK" clicks there; a long
// press is a right click (the context menu); a frame is a left-button drag.
//
// A stylus is a direct pointer: hovering moves the mouse (snaps and
// highlights before the touch), touching is the left button, its button
// with a touch the right one, the eraser end deletes what it touches. While
// it is near, fingers only navigate (palm rejection).

#include "app/AppContext.h"

#include <map>
#include <vector>

namespace bz::app::mobile {

/// How far above the finger the cursor's tip is (dp).
inline constexpr float kCursorReach = 48.0f;   ///< as in the mockup: the drop's round end 48 dp below its point (default)

/// The tip's distance above the finger now (dp): the user's setting at the labels' scale.
float CursorReach(const AppContext& app);

class TouchInput {
public:
    void Push(const PointerEvent& event) { myQueue.push_back(event); }

    /// Before ImGui::NewFrame: routes what came since the last frame — into
    /// ImGui's input queue and into app.shell.mobile.gestures for the view.
    void BeginFrame(AppContext& app, double now);

    /// Pointers are down or clicks are queued: keep drawing frames.
    bool Busy() const;

private:
    enum class Route : unsigned char { Ui, View, Pen, Mouse, Numpad, Ignored };
    struct UiFinger {
        int    id = -1;
        ImVec2 start{0, 0}, at{0, 0};
        double since = 0;
        enum class Mode : unsigned char { Undecided, Scroll, Drag, Held } mode = Mode::Undecided;
        bool dragOnly = false;   ///< the panel there does not scroll: any direction drags (the cube)
    };

    void Route1(AppContext& app, const PointerEvent& e);
    void Wheel(float dx, float dy);   ///< scroll what is under the mouse by this finger travel (dp)
    void Fling(double now);
    void UiEvent(const PointerEvent& e);
    void PenEvent(AppContext& app, const PointerEvent& e);
    void MouseEvent(const PointerEvent& e);
    void ApplyGestures(AppContext& app);
    void RunRowButtons(AppContext& app);
    void PlaceMouse(AppContext& app);

    // ImGui's input queue, with the touch screen as the mouse's source.
    void Pos(ImVec2 at, ImGuiMouseSource source = ImGuiMouseSource_TouchScreen);
    void Button(int button, bool down, ImGuiMouseSource source = ImGuiMouseSource_TouchScreen);
    void Click(ImVec2 at, int button);

    std::vector<PointerEvent> myQueue;
    std::map<int, Route>      myRoutes;
    GestureRecognizer         myGestures;
    UiFinger                  myUi;
    // A list flung: it keeps scrolling, slowing down (dp/s), where the finger left
    float  myFlingY = 0, myFlingX = 0;
    ImVec2 myFlingAt{0, 0};
    double myFlingTime = 0;
    double myLastMoveTime = 0;
    // The precise cursor (speed < 1): the tip moves a part of the finger's travel from where it was
    bool   myRelative = false;
    ImVec2 myAnchorFinger{0, 0}, myAnchorTip{0, 0};
    float  myRelativeTravel = 0;
    double myRelativeSince = 0;
    ImVec2                    myMouse{-1, -1};   ///< last position given to ImGui
    bool                      myMouseKnown = false;
    int                       myMouseButtons = 0;   ///< a real mouse's buttons held
    bool                      myMouseInUse = false; ///< a real mouse moved last: it points, not the cursor
    // Stylus
    int    myPenId = -1;
    ImVec2 myPenStart{0, 0};
    bool   myPenButton = false;   ///< its button held at the touch: right click or a frame
    bool   myPenFraming = false;
    int    myQueued = 0;          ///< frames still needed for queued clicks
    float  mySlop = 10.0f;        ///< the user's tap slop (dp)
    float  myScrollSpeed = 1.0f;
    // The numpad (its keys are not ImGui items: see mobile/Numpad.cpp)
    int    myNumpadId = -1;       ///< the finger on a key
    double myNumpadSince = 0, myNumpadRepeat = 0;
};

/// The drop cursor is in use now: the phone layout, a document open, and a
/// sketch (always) or the cursor's button on.
bool CursorActive(const AppContext& app);
/// The sketch tool places points (draws, moves, copies): only with the cursor —
/// a bare finger is too coarse for it (author, 17.10). Picking tools (select,
/// dimension, trim, constraints…) may go without.
bool SketchToolNeedsCursor(const AppContext& app);

} // namespace bz::app::mobile
