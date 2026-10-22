// The sketch clipboard (22.10.2026): shapes taken with a base point, put into
// another sketch (of another part) with that point where the cursor is.
#include "sketch/Clipboard.h"
#include "sketch/Shapes.h"
#include "sketch/Solver.h"

#include <doctest.h>

#include <algorithm>
#include <string>

using namespace bz;
using namespace bz::sketch;

namespace {

std::size_t Count(const Sketch& sk, ConstraintType type) {
    return static_cast<std::size_t>(std::count_if(sk.constraints.begin(), sk.constraints.end(), [&](const Constraint& c) { return c.type == type; }));
}

ObjectId AddLength(Sketch& sk, IdGenerator& ids, ObjectId line, const std::string& expression, double value) {
    Constraint c;
    c.type = ConstraintType::Length;
    c.refs = {line, {}, {}};
    c.parameter.name = "d1";
    c.parameter.expression = expression;
    c.parameter.quantity = Quantity::Length;
    c.parameter.value = value;
    return sk.AddConstraint(ids, c);
}

} // namespace

TEST_CASE("a rectangle copied with a base point at its corner goes where the cursor is") {
    Sketch from;
    IdGenerator fromIds;
    const RectangleIds r = AddCornerRectangle(from, fromIds, from.AddPoint(fromIds, {10, 10}), from.AddPoint(fromIds, {50, 30}));
    AddLength(from, fromIds, r.sides[0], "w * 2", 40);   // a size on a variable of the part it came from
    const std::vector<ObjectId> chosen(r.sides.begin(), r.sides.end());
    const Clipboard clip = CopyToClipboard(from, chosen, {10, 10});
    REQUIRE_FALSE(clip.Empty());
    CHECK(clip.shapes.lines.size() == 4);

    Sketch to;   // another sketch, another part: its own ids
    IdGenerator toIds;
    toIds.Reserve(ObjectId{500});
    to.AddPoint(toIds, {0, 0});
    int names = 6;
    const auto next = [&] { return "d" + std::to_string(++names); };
    const std::vector<ObjectId> made = PasteFromClipboard(to, toIds, clip, {100, -20}, next);
    CHECK(made.size() == 4);
    CHECK(to.lines.size() == 4);
    // The base corner on the cursor, the rest as it was around it.
    std::vector<Vec2> corners;
    for (const Point& p : to.points) corners.push_back(p.pos);
    const auto has = [&](Vec2 v) {
        return std::any_of(corners.begin(), corners.end(), [&](Vec2 c) { return (c - v).Length() < 1e-9; });
    };
    CHECK(has({100, -20}));
    CHECK(has({140, 0}));
    // Its shape constraints come along; the size is a number now, named in the new part.
    CHECK(Count(to, ConstraintType::Horizontal) == Count(from, ConstraintType::Horizontal));
    CHECK(Count(to, ConstraintType::Vertical) == Count(from, ConstraintType::Vertical));
    const auto size = std::find_if(to.constraints.begin(), to.constraints.end(), [](const Constraint& c) { return c.type == ConstraintType::Length; });
    REQUIRE(size != to.constraints.end());
    CHECK(size->parameter.expression == "40mm");
    CHECK(size->parameter.name == "d7");
    CHECK(Solve(to).converged);
}

TEST_CASE("pasted as many times as wanted, each a copy of its own") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId line = sk.AddLine(ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {10, 0}));
    const Clipboard clip = CopyToClipboard(sk, std::vector<ObjectId>{line}, {0, 0});
    PasteFromClipboard(sk, ids, clip, {0, 10}, {});
    PasteFromClipboard(sk, ids, clip, {0, 20}, {});
    CHECK(sk.lines.size() == 3);
    CHECK(sk.points.size() == 6);
}

TEST_CASE("what stays behind: a fixed place, ties to shapes not copied") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 0}), c = sk.AddPoint(ids, {10, 10});
    const ObjectId l1 = sk.AddLine(ids, a, b), l2 = sk.AddLine(ids, b, c);
    Constraint fix;
    fix.type = ConstraintType::Fix;
    fix.refs = {a, {}, {}};
    sk.AddConstraint(ids, fix);
    Constraint perp;
    perp.type = ConstraintType::Perpendicular;
    perp.refs = {l1, l2, {}};
    sk.AddConstraint(ids, perp);
    const Clipboard clip = CopyToClipboard(sk, std::vector<ObjectId>{l1}, {0, 0});
    CHECK(clip.shapes.lines.size() == 1);
    CHECK(clip.shapes.constraints.empty());   // the fix and the tie to l2 left behind
}

TEST_CASE("a projected model edge comes as an ordinary line") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId line = sk.AddLine(ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {5, 5}));
    sk.FindLine(line)->projected = true;
    for (Point& p : sk.points) p.projected = true;
    const Clipboard clip = CopyToClipboard(sk, std::vector<ObjectId>{line}, {0, 0});
    Sketch to;
    IdGenerator toIds;
    PasteFromClipboard(to, toIds, clip, {1, 1}, {});
    REQUIRE(to.lines.size() == 1);
    CHECK_FALSE(to.lines[0].projected);
    CHECK(std::none_of(to.points.begin(), to.points.end(), [](const Point& p) { return p.projected; }));
}

TEST_CASE("a text comes whole, once") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId text = sk.AddText(ids, {0, 0}, {30, 8}, "BreezeCAD", 5);
    const std::size_t lines = sk.lines.size(), points = sk.points.size();
    const Clipboard clip = CopyToClipboard(sk, std::vector<ObjectId>{text}, {0, 0});
    Sketch to;
    IdGenerator toIds;
    PasteFromClipboard(to, toIds, clip, {10, 10}, {});
    REQUIRE(to.texts.size() == 1);
    CHECK(to.texts[0].text == "BreezeCAD");
    CHECK(to.lines.size() == lines);    // its own sides, not twice
    CHECK(to.points.size() == points);
    CHECK(to.PositionOf(to.texts[0].corners[0]).x == doctest::Approx(10.0));
}
