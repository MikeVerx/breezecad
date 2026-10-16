// The finger gestures of the mobile version (src/app/mobile/Gestures.h,
// docs/ADR/0004-mobile-touch-ui.md "Жесты"), with made-up touches.
#include "app/mobile/Gestures.h"

#include <doctest.h>

using bz::Vec2;
using bz::app::mobile::GestureRecognizer;
using bz::app::mobile::GestureStep;

namespace {
GestureRecognizer Recognizer(bool cursorOn, bool boxMode = false) {
    GestureRecognizer g;
    g.SetMode({cursorOn, boxMode, false});
    return g;
}
} // namespace

TEST_CASE("Gestures: a quick still touch is a tap") {
    GestureRecognizer g = Recognizer(false);
    g.Down(1, {100, 100}, 0.0);
    g.Move(1, {103, 101}, 0.05);
    g.Up(1, {103, 101}, 0.12);
    const GestureStep s = g.Take();
    REQUIRE(s.tap);
    CHECK(s.tap->x == doctest::Approx(103));
    CHECK(s.orbit == Vec2{0, 0});
}

TEST_CASE("Gestures: a slow touch is no tap, a held one is the menu") {
    GestureRecognizer g = Recognizer(false);
    g.Down(1, {50, 50}, 0.0);
    g.Tick(0.3);
    CHECK_FALSE(g.Take().longPress);
    g.Tick(0.6);
    const GestureStep held = g.Take();
    REQUIRE(held.longPress);
    g.Up(1, {50, 50}, 0.7);
    CHECK_FALSE(g.Take().tap);
}

TEST_CASE("Gestures: cursor off — one finger turns, two move and pinch") {
    GestureRecognizer g = Recognizer(false);
    g.Down(1, {100, 100}, 0.0);
    g.Move(1, {130, 100}, 0.1);
    GestureStep s = g.Take();
    CHECK(s.orbitStart);
    CHECK(s.orbit.x == doctest::Approx(30));
    g.Up(1, {130, 100}, 0.2);
    g.Take();

    g.Down(1, {100, 100}, 1.0);
    g.Down(2, {200, 100}, 1.0);
    g.Move(1, {90, 120}, 1.1);
    g.Move(2, {210, 120}, 1.1);
    s = g.Take();
    CHECK(s.orbit == Vec2{0, 0});
    CHECK(s.pan.y == doctest::Approx(20));
    CHECK(s.zoom == doctest::Approx(120.0 / 100.0));
}

TEST_CASE("Gestures: cursor on — one finger leads the cursor, two turn, three move") {
    GestureRecognizer g = Recognizer(true);
    g.Down(1, {100, 100}, 0.0);
    GestureStep s = g.Take();
    REQUIRE(s.cursorFinger);
    CHECK(s.cursorStarted);
    g.Move(1, {140, 90}, 0.1);
    s = g.Take();
    REQUIRE(s.cursorFinger);
    CHECK(s.cursorFinger->x == doctest::Approx(140));
    CHECK(s.orbit == Vec2{0, 0});
    g.Up(1, {140, 90}, 0.4);
    CHECK(g.Take().cursorEnded);

    // Two fingers: the first did not move yet — the cursor goes back.
    g.Down(1, {100, 100}, 1.0);
    g.Take();
    g.Down(2, {200, 100}, 1.02);
    s = g.Take();
    CHECK(s.cursorRevert);
    g.Move(1, {120, 100}, 1.1);
    g.Move(2, {220, 100}, 1.1);
    s = g.Take();
    CHECK(s.orbit.x > 0);
    CHECK(s.pan == Vec2{0, 0});
    g.Up(1, {120, 100}, 1.2);
    g.Up(2, {220, 100}, 1.2);
    g.Take();

    g.Down(1, {100, 100}, 2.0);
    g.Down(2, {150, 100}, 2.0);
    g.Down(3, {200, 100}, 2.0);
    g.Take();
    for (int id = 1; id <= 3; ++id) g.Move(id, {100.0 + 50.0 * (id - 1), 130}, 2.1);
    s = g.Take();
    CHECK(s.pan.y == doctest::Approx(30));
    CHECK(s.orbit == Vec2{0, 0});
}

TEST_CASE("Gestures: two and three finger taps are undo and redo") {
    GestureRecognizer g = Recognizer(true);
    g.Down(1, {100, 100}, 0.0);
    g.Down(2, {160, 100}, 0.01);
    g.Up(1, {100, 100}, 0.1);
    g.Up(2, {160, 100}, 0.11);
    GestureStep s = g.Take();
    CHECK(s.undo);
    CHECK_FALSE(s.redo);
    CHECK_FALSE(s.tap);

    g.Down(1, {100, 100}, 1.0);
    g.Down(2, {150, 100}, 1.0);
    g.Down(3, {200, 100}, 1.0);
    g.Up(3, {200, 100}, 1.1);
    g.Up(2, {150, 100}, 1.1);
    g.Up(1, {100, 100}, 1.1);
    s = g.Take();
    CHECK(s.redo);
    CHECK_FALSE(s.undo);
}

TEST_CASE("Gestures: a frame with one finger in frame mode, cursor off") {
    GestureRecognizer g = Recognizer(false, true);
    g.Down(1, {10, 10}, 0.0);
    g.Move(1, {80, 60}, 0.1);
    GestureStep s = g.Take();
    REQUIRE(s.boxFrom);
    REQUIRE(s.boxTo);
    CHECK(s.boxFrom->x == doctest::Approx(10));
    CHECK(s.orbit == Vec2{0, 0});
    g.Up(1, {80, 60}, 0.3);
    CHECK(g.Take().boxEnded);
}

TEST_CASE("Gestures: with a stylus near, fingers only navigate") {
    GestureRecognizer g;
    g.SetMode({true, false, true});
    g.Down(1, {100, 100}, 0.0);
    GestureStep s = g.Take();
    CHECK_FALSE(s.cursorFinger);
    g.Move(1, {140, 100}, 0.1);
    CHECK(g.Take().orbit.x > 0);
    g.Up(1, {140, 100}, 0.2);
    g.Down(1, {100, 100}, 1.0);
    g.Up(1, {100, 100}, 1.05);
    CHECK_FALSE(g.Take().tap);
}
