#include "app/sketch/DimensionArrange.h"
#include "app/sketch/DimensionLayout.h"

#include <doctest.h>

using namespace bz;
using namespace bz::app;

namespace {

sketch::Constraint LengthOf(ObjectId line, double value, Vec2 offset) {
    sketch::Constraint c;
    c.type = sketch::ConstraintType::Length;
    c.refs = {line, {}, {}};
    c.parameter.expression = std::to_string(static_cast<int>(value));
    c.parameter.value = value;
    c.labelOffset = offset;
    return c;
}

/// Labels as boxes the size SketchOverlay draws them, for the overlap check.
bool LabelsOverlap(const sketch::Sketch& sk, const sketch::Constraint& a, const sketch::Constraint& b, const ArrangeScale& s) {
    const UnitSystem units;
    const auto la = LayoutDimension(sk, a, units), lb = LayoutDimension(sk, b, units);
    REQUIRE(la);
    REQUIRE(lb);
    const double w = 40 * s.mmPerPixel, h = 26 * s.mmPerPixel;
    return std::abs(la->label.x - lb->label.x) < w && std::abs(la->label.y - lb->label.y) < h;
}

} // namespace

TEST_CASE("a new dimension keeps the side it was put on and comes in close to its line") {
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {100, 0});
    const ObjectId line = sk.AddLine(ids, a, b);
    // Tapped far above the line: the label comes down near it, still above.
    sketch::Constraint c = LengthOf(line, 100, {0, 80});
    const ArrangeScale scale{0.25, 1.0};   // 1 px = 0.25 mm
    ArrangeDimension(sk, c, UnitSystem{}, scale);
    CHECK(c.labelOffset.y > 0);
    CHECK(c.labelOffset.Length() < 20);   // a step or two of 24 px (6 mm), not 80 mm
}

TEST_CASE("two dimensions on top of each other are moved apart") {
    // Two horizontal lines 5 mm apart, both dimensioned with the label at the same height:
    // the texts sat on each other.
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId l1 = sk.AddLine(ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {100, 0}));
    const ObjectId l2 = sk.AddLine(ids, sk.AddPoint(ids, {0, 5}), sk.AddPoint(ids, {100, 5}));
    sk.AddConstraint(ids, LengthOf(l1, 100, {0, 8}));
    sk.AddConstraint(ids, LengthOf(l2, 100, {0, 3}));
    const ArrangeScale scale{0.25, 1.0};
    REQUIRE(LabelsOverlap(sk, sk.constraints[0], sk.constraints[1], scale));

    ArrangeDimensions(sk, UnitSystem{}, scale);
    CHECK_FALSE(LabelsOverlap(sk, sk.constraints[0], sk.constraints[1], scale));
}

TEST_CASE("a dimension label does not land on the sketch's own lines") {
    // A square 40 mm; the bottom side's dimension wished inside the square,
    // right where the top side runs at this zoom: it ends up clear of both sides.
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId p0 = sk.AddPoint(ids, {0, 0}), p1 = sk.AddPoint(ids, {40, 0});
    const ObjectId p2 = sk.AddPoint(ids, {40, 40}), p3 = sk.AddPoint(ids, {0, 40});
    const ObjectId bottom = sk.AddLine(ids, p0, p1);
    sk.AddLine(ids, p1, p2);
    sk.AddLine(ids, p2, p3);
    sk.AddLine(ids, p3, p0);
    sketch::Constraint c = LengthOf(bottom, 40, {0, 40});
    const ArrangeScale scale{0.5, 1.0};   // 1 px = 0.5 mm: a label is ~20 × 13 mm
    ArrangeDimension(sk, c, UnitSystem{}, scale);
    const auto l = LayoutDimension(sk, c, UnitSystem{});
    REQUIRE(l);
    const double halfH = 13 * scale.mmPerPixel;
    CHECK((l->label.y + halfH < 40 || l->label.y - halfH > 40));   // not across the top side
    CHECK((l->label.y - halfH > 0 || l->label.y + halfH < 0));     // not across its own line
}
