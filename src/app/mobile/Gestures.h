#pragma once
// Finger gestures over the 3D view (docs/ADR/0004-mobile-touch-ui.md, "Жесты"),
// as plain logic: fingers in, what they mean out. No ImGui, no OCCT —
// tests/app/GestureTests.cpp drives it with made-up touches.
//
// What dragging fingers do is the user's (Settings → "Жесты", one set with
// the cursor off, one with it on); the defaults:
//
//                       cursor off            cursor on
//   tap                 select (click there)  move the cursor there
//   drag, 1 finger      orbit                 drag the cursor (always)
//   2 fingers           pan                   pan
//   3 fingers           pan                   orbit
//   pinch               zoom                  zoom
//   long press          context menu          context menu
//   a tap with 2 fingers — undo, with 3 — redo (TouchInput runs what the user chose).
//
// A tap: the fingers barely moved (Tuning::tapSlop) and left before the long
// press; a tap of two or three fingers also within kTapSeconds.

#include "core/Math.h"

#include <optional>
#include <vector>

namespace bz::app::mobile {

inline constexpr double kTapSeconds = 0.35;        ///< a tap of two or three fingers is shorter
inline constexpr double kTapSlopDp = 10.0;         ///< a tap moves less (default; Tuning)
inline constexpr double kLongPressSeconds = 0.5;   ///< a press held this long, still: the context menu (default; Tuning)

/// What dragging fingers do to the view.
enum class FingerDrag : unsigned char { None, Orbit, Pan, Zoom };

/// The drags of one set ("курсор выкл" or "курсор вкл").
struct DragMap {
    FingerDrag one = FingerDrag::Orbit;   ///< one finger (with the cursor on it always leads the cursor)
    FingerDrag two = FingerDrag::Pan;
    FingerDrag three = FingerDrag::Pan;   ///< three or more
    bool       pinch = true;              ///< two fingers apart or together zoom
};

/// What a gesture did since the last Take(): deltas to apply, events to run.
struct GestureStep {
    Vec2   orbit{0, 0};   ///< turn the view by this many dp of finger travel
    Vec2   pan{0, 0};     ///< move the view by this many dp
    double zoom = 1.0;    ///< scale the view by this about zoomAt (pinch, or a drag set to zoom)
    Vec2   zoomAt{0, 0};
    /// A new orbit started (or another turn after a pause): pick its pivot here.
    std::optional<Vec2> orbitStart;

    std::optional<Vec2> tap;         ///< one finger tapped here
    std::optional<Vec2> longPress;   ///< one finger held still here
    bool tap2 = false;               ///< two fingers tapped (default: undo)
    bool tap3 = false;               ///< three fingers tapped (default: redo)

    /// The finger that leads the cursor (cursor on, one finger), where it is now.
    std::optional<Vec2> cursorFinger;
    bool cursorStarted = false;   ///< it touched down this time (the cursor jumps under it)
    /// A second finger came before the first one moved: the cursor goes back
    /// to where it was before this touch (the fingers meant a turn, not it).
    bool cursorRevert = false;
    bool cursorEnded = false;     ///< the leading finger left

    /// Cursor off, frame-selection mode: one finger draws a frame.
    std::optional<Vec2> boxFrom, boxTo;
    bool boxEnded = false;

    bool Empty() const {
        return orbit.x == 0 && orbit.y == 0 && pan.x == 0 && pan.y == 0 && zoom == 1.0 && !orbitStart && !tap && !longPress &&
               !tap2 && !tap3 && !cursorFinger && !cursorEnded && !cursorRevert && !boxTo && !boxEnded;
    }
};

class GestureRecognizer {
public:
    struct Mode {
        bool cursorOn = false;   ///< the drop cursor is on (sketch: always)
        bool boxMode = false;    ///< "Выделение рамкой" is on (cursor off: one finger draws the frame)
        bool fingersOnlyNavigate = false;   ///< a stylus is near (palm rejection): no taps, cursor or frames
        DragMap drags;                      ///< what drags do now (the set for the cursor's state)
    };

    /// The defaults of the two sets.
    static DragMap DefaultDrags(bool cursorOn) {
        return cursorOn ? DragMap{FingerDrag::Orbit, FingerDrag::Pan, FingerDrag::Orbit, true} : DragMap{};
    }

    /// How the fingers are read (the user's "Отзывчивость" settings).
    struct Tuning {
        double tapSlop = kTapSlopDp;          ///< dp a finger may move and still tap or long-press
        double longPress = kLongPressSeconds; ///< s
    };

    void SetMode(const Mode& mode) { myMode = mode; }
    void SetTuning(const Tuning& tuning) { myTuning = tuning; }
    const Mode& GetMode() const { return myMode; }

    void Down(int id, Vec2 at, double time);
    void Move(int id, Vec2 at, double time);
    void Up(int id, Vec2 at, double time);
    /// The system took the touches (a system gesture): everything stops, nothing fires.
    void Cancel();
    /// Notices a press held still (long press) without new events.
    void Tick(double time);

    /// What happened since the last call.
    GestureStep Take();

    bool Active() const { return !myFingers.empty(); }
    int  FingerCount() const { return static_cast<int>(myFingers.size()); }

private:
    struct Finger {
        int  id = 0;
        Vec2 at, start;
    };
    Finger* Find(int id);
    void    Reset();
    Vec2    Centre() const;
    double  Spread() const;

    Mode                myMode;
    Tuning              myTuning;
    std::vector<Finger> myFingers;
    GestureStep         myStep;

    // The touch from the first finger down to the last one up.
    double myStartTime = 0.0;
    int    myMaxFingers = 0;
    bool   myMoved = false;       ///< a finger left its slop circle
    bool   myConsumed = false;    ///< a long press fired: nothing else from this touch
    bool   myCursorLeading = false;   ///< the cursor follows the first finger
    bool   myBoxing = false;
    bool   myOrbiting = false;
    // References for the deltas, from the last event (reset when the finger count changes).
    Vec2   myLastCentre{0, 0};
    double myLastSpread = 0.0;
};

} // namespace bz::app::mobile
