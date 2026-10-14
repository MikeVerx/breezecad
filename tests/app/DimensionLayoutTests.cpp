#include "app/sketch/DimensionLayout.h"

#include <doctest.h>

using namespace bz;
using namespace bz::app;

namespace {

/// A horizontal line from (0, 0) to (100, 0) with an aligned length dimension.
struct Dimensioned {
    sketch::Sketch     sk;
    sketch::Constraint c;
    IdGenerator        ids;
    Dimensioned() {
        const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {100, 0});
        const ObjectId line = sk.AddLine(ids, a, b);
        c.type = sketch::ConstraintType::Length;
        c.refs = {line, {}, {}};
        c.parameter.expression = "100";
        c.parameter.value = 100;
    }
};

/// The dimension line reaches `x` at height `y`.
bool Reaches(const DimensionLayout& l, double x, double y) {
    for (const auto& s : l.lines)
        if (std::abs(s.a.y - y) < 1e-9 && std::abs(s.b.y - y) < 1e-9 && std::min(s.a.x, s.b.x) <= x + 1e-9 &&
            std::max(s.a.x, s.b.x) >= x - 1e-9)
            return true;
    return false;
}

} // namespace

TEST_CASE("a dimension label moves along and across; the line follows it out past the extension lines") {
    Dimensioned d;
    const UnitSystem units;
    d.c.labelOffset = {20, 15};   // over the line, a bit to the right of the middle
    auto l = LayoutDimension(d.sk, d.c, units);
    REQUIRE(l);
    CHECK(l->label.x == doctest::Approx(70.0));
    CHECK(l->label.y == doctest::Approx(15.0));
    CHECK(Reaches(*l, 0, 15));
    CHECK(Reaches(*l, 100, 15));

    d.c.labelOffset = {90, 15};   // dragged past the right extension line
    l = LayoutDimension(d.sk, d.c, units);
    REQUIRE(l);
    CHECK(l->label.x == doctest::Approx(140.0));
    CHECK(Reaches(*l, 140, 15));   // before: the line stopped at 100 and the text floated
    d.c.labelOffset = {-80, -10};   // past the left one, below
    l = LayoutDimension(d.sk, d.c, units);
    REQUIRE(l);
    CHECK(Reaches(*l, -30, -10));
}

TEST_CASE("a radius label goes round its circle") {
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId centre = sk.AddPoint(ids, {0, 0});
    const ObjectId circle = sk.AddCircle(ids, centre, 10);
    sketch::Constraint c;
    c.type = sketch::ConstraintType::Radius;
    c.refs = {circle, {}, {}};
    c.parameter.value = 10;
    c.labelOffset = {0, -25};
    const auto l = LayoutDimension(sk, c, UnitSystem{});
    REQUIRE(l);
    CHECK(l->label.y == doctest::Approx(-25.0));
    REQUIRE_FALSE(l->arrows.empty());
    CHECK(l->arrows.front().tip.y == doctest::Approx(-10.0));   // on the circle, toward the label
}
