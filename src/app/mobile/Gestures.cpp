#include "app/mobile/Gestures.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bz::app::mobile {

GestureRecognizer::Finger* GestureRecognizer::Find(int id) {
    for (Finger& f : myFingers)
        if (f.id == id) return &f;
    return nullptr;
}

Vec2 GestureRecognizer::Centre() const {
    Vec2 sum{0, 0};
    for (const Finger& f : myFingers) sum = sum + f.at;
    return myFingers.empty() ? sum : sum / static_cast<double>(myFingers.size());
}

double GestureRecognizer::Spread() const {
    // The first two fingers: a pinch is made with two, a third rests.
    return myFingers.size() >= 2 ? (myFingers[1].at - myFingers[0].at).Length() : 0.0;
}

void GestureRecognizer::Reset() {
    myFingers.clear();
    myMaxFingers = 0;
    myMoved = myConsumed = myCursorLeading = myBoxing = myOrbiting = false;
}

void GestureRecognizer::Down(int id, Vec2 at, double time) {
    if (Find(id)) return;
    if (myFingers.empty()) {
        Reset();
        myStartTime = time;
    }
    myFingers.push_back({id, at, at});
    myMaxFingers = std::max(myMaxFingers, static_cast<int>(myFingers.size()));
    myLastCentre = Centre();
    myLastSpread = Spread();

    if (myFingers.size() == 1) {
        if (myMode.cursorOn && !myMode.fingersOnlyNavigate) {
            // The cursor is called here at once: its circle under the finger.
            myCursorLeading = true;
            myStep.cursorFinger = at;
            myStep.cursorStarted = true;
        }
        return;
    }
    // A second finger: the touch is a turn, a move or a zoom now.
    if (myCursorLeading) {
        myCursorLeading = false;
        if (!myMoved) myStep.cursorRevert = true;   // the first finger was the start of this, not a cursor move
        else myStep.cursorEnded = true;
    }
    if (myBoxing) {
        myBoxing = false;
        myStep.boxEnded = true;
    }
    myOrbiting = false;
}

void GestureRecognizer::Move(int id, Vec2 at, double) {
    Finger* f = Find(id);
    if (!f) return;
    f->at = at;
    if ((at - f->start).Length() > myTuning.tapSlop) myMoved = true;
    if (myConsumed) return;

    const Vec2   centre = Centre();
    const double spread = Spread();
    const Vec2   delta = centre - myLastCentre;
    const int    n = static_cast<int>(myFingers.size());

    // A drag as the user set it: turn, move, or zoom (up brings closer).
    const auto apply = [&](FingerDrag what, Vec2 at0) {
        switch (what) {
            case FingerDrag::Orbit:
                if (!myOrbiting) {
                    myOrbiting = true;
                    myStep.orbitStart = at0;
                }
                myStep.orbit = myStep.orbit + delta;
                break;
            case FingerDrag::Pan: myStep.pan = myStep.pan + delta; break;
            case FingerDrag::Zoom:
                myStep.zoom *= std::pow(1.006, -delta.y);
                myStep.zoomAt = myFingers.size() == 1 ? myFingers[0].start : centre;
                break;
            case FingerDrag::None: break;
        }
    };

    if (n == 1) {
        if (myCursorLeading) {
            myStep.cursorFinger = at;   // 1:1 from the first touch, slop or not
        } else if (myMoved && !myMode.fingersOnlyNavigate && myMode.boxMode && !myMode.cursorOn) {
            if (!myBoxing) {
                myBoxing = true;
                myStep.boxFrom = f->start;
            }
            myStep.boxTo = at;
        } else if (myMoved && (!myMode.cursorOn || myMode.fingersOnlyNavigate)) {
            // A stylus near: one finger turns whatever the set says (it cannot lead the cursor).
            apply(myMode.fingersOnlyNavigate && myMode.drags.one == FingerDrag::None ? FingerDrag::Orbit : myMode.drags.one, f->start);
        }
    } else {
        // Two or three fingers: as the set says. A pinch zooms on top.
        if (myMoved) {
            apply(n == 2 ? myMode.drags.two : myMode.drags.three, centre);
            if (n == 2 && myMode.drags.pinch && myLastSpread > 1.0 && spread > 1.0) {
                myStep.zoom *= spread / myLastSpread;
                myStep.zoomAt = centre;
            }
        }
    }
    myLastCentre = centre;
    myLastSpread = spread;
}

void GestureRecognizer::Up(int id, Vec2 at, double time) {
    Finger* f = Find(id);
    if (!f) return;
    f->at = at;
    const bool last = myFingers.size() == 1;
    if (last) {
        // One finger taps until the long press would fire (a slow, deliberate
        // tap is still a tap: 0.25 s missed many on the phone); two or three
        // fingers must be quick, so a pinch that barely moved is no undo.
        const bool still = !myMoved && !myConsumed;
        const bool quick = still && time - myStartTime < (myMaxFingers == 1 ? myTuning.longPress : kTapSeconds);
        if (myCursorLeading) myStep.cursorEnded = true;
        if (myBoxing) myStep.boxEnded = true;
        // With the cursor on, a tap only moves it there (it already did): no click.
        if (quick && !myMode.fingersOnlyNavigate && !myCursorLeading && myMaxFingers == 1) myStep.tap = at;
        if (quick) {
            if (myMaxFingers == 2) myStep.tap2 = true;
            if (myMaxFingers == 3) myStep.tap3 = true;
        }
        Reset();
        return;
    }
    myFingers.erase(myFingers.begin() + (f - myFingers.data()));
    // Fewer fingers: the deltas count from where the rest are now.
    myLastCentre = Centre();
    myLastSpread = Spread();
    myOrbiting = false;
}

void GestureRecognizer::Cancel() {
    if (myCursorLeading) myStep.cursorEnded = true;
    if (myBoxing) myStep.boxEnded = true;
    Reset();
}

void GestureRecognizer::Tick(double time) {
    if (myFingers.size() != 1 || myMaxFingers != 1 || myMoved || myConsumed) return;
    if (time - myStartTime < myTuning.longPress) return;
    myConsumed = true;
    myStep.longPress = myFingers[0].at;
}

GestureStep GestureRecognizer::Take() { return std::exchange(myStep, GestureStep{}); }

} // namespace bz::app::mobile
