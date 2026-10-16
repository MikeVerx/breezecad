#include "app/mobile/Gestures.h"

#include <algorithm>
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
    if ((at - f->start).Length() > kTapSlopDp) myMoved = true;
    if (myConsumed) return;

    const Vec2   centre = Centre();
    const double spread = Spread();
    const Vec2   delta = centre - myLastCentre;
    const int    n = static_cast<int>(myFingers.size());

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
            if (!myOrbiting) {
                myOrbiting = true;
                myStep.orbitStart = f->start;
            }
            myStep.orbit = myStep.orbit + delta;
        }
    } else {
        // Two fingers turn with the cursor on (one finger is the cursor's),
        // pan with it off; three always pan. A pinch zooms either way.
        const bool turn = myMode.cursorOn && !myMode.fingersOnlyNavigate && n == 2;
        if (myMoved) {
            if (turn) {
                if (!myOrbiting) {
                    myOrbiting = true;
                    myStep.orbitStart = centre;
                }
                myStep.orbit = myStep.orbit + delta;
            } else {
                myStep.pan = myStep.pan + delta;
            }
            if (n == 2 && myLastSpread > 1.0 && spread > 1.0) {
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
        const bool quick = !myMoved && !myConsumed && time - myStartTime < kTapSeconds;
        if (myCursorLeading) myStep.cursorEnded = true;
        if (myBoxing) myStep.boxEnded = true;
        if (quick && !myMode.fingersOnlyNavigate) {
            if (myMaxFingers == 1) myStep.tap = at;
        }
        if (quick) {
            if (myMaxFingers == 2) myStep.undo = true;
            if (myMaxFingers == 3) myStep.redo = true;
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
    if (time - myStartTime < kLongPressSeconds) return;
    myConsumed = true;
    myStep.longPress = myFingers[0].at;
}

GestureStep GestureRecognizer::Take() { return std::exchange(myStep, GestureStep{}); }

} // namespace bz::app::mobile
