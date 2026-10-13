#include "app/sketch/DimensionLayout.h"

#include "app/Format.h"

#include <cmath>

namespace bz::app {
namespace {

using sketch::ConstraintType;

/// Endpoints measured by a linear dimension.
std::optional<std::pair<Vec2, Vec2>> LinearEnds(const sketch::Sketch& sk, const sketch::Constraint& c) {
    if (c.type == ConstraintType::Length) {
        const sketch::Line* l = sk.FindLine(c.refs[0]);
        if (!l) return std::nullopt;
        return std::pair{sk.PositionOf(l->start), sk.PositionOf(l->end)};
    }
    if (c.type == ConstraintType::PointLineDistance) {
        const sketch::Line* l = sk.FindLine(c.refs[1]);
        if (!l || !sk.FindPoint(c.refs[0])) return std::nullopt;
        const Vec2 p = sk.PositionOf(c.refs[0]);
        const Vec2 a = sk.PositionOf(l->start), b = sk.PositionOf(l->end);
        const Vec2 d = (b - a).Normalized();
        const Vec2 foot = a + d * (p - a).Dot(d);
        // To an осевая: the whole diameter, from the point to its mirror image.
        if (l->centerline) return std::pair{p, foot * 2.0 - p};
        return std::pair{p, foot};
    }
    if (!sk.FindPoint(c.refs[0]) || !sk.FindPoint(c.refs[1])) return std::nullopt;
    return std::pair{sk.PositionOf(c.refs[0]), sk.PositionOf(c.refs[1])};
}

/// Lines meeting at a vertex for an angle dimension: vertex and the two
/// directions pointing away from it toward each line's far end.
std::optional<std::tuple<Vec2, Vec2, Vec2>> AngleParts(const sketch::Sketch& sk, const sketch::Constraint& c) {
    const sketch::Line* l1 = sk.FindLine(c.refs[0]);
    const sketch::Line* l2 = sk.FindLine(c.refs[1]);
    if (!l1 || !l2) return std::nullopt;
    const Vec2 a1 = sk.PositionOf(l1->start), b1 = sk.PositionOf(l1->end);
    const Vec2 a2 = sk.PositionOf(l2->start), b2 = sk.PositionOf(l2->end);
    const Vec2 d1 = b1 - a1, d2 = b2 - a2;
    const double denom = d1.Cross(d2);
    if (std::abs(denom) < 1e-9) return std::nullopt;
    const double t = (a2 - a1).Cross(d2) / denom;
    const Vec2 vertex = a1 + d1 * t;
    // Point each direction from the vertex toward the line's far end.
    const Vec2 u1 = ((b1 - vertex).Length() > (a1 - vertex).Length() ? b1 - vertex : a1 - vertex).Normalized();
    const Vec2 u2 = ((b2 - vertex).Length() > (a2 - vertex).Length() ? b2 - vertex : a2 - vertex).Normalized();
    return std::tuple{vertex, u1, u2};
}

std::string Label(const sketch::Sketch& sk, const sketch::Constraint& c, double value, const UnitSystem& units) {
    std::string text;
    if (c.type == ConstraintType::Angle) {
        text = FormatValue(std::abs(value), Quantity::Angle, units, 2) + (units.angle == AngleUnit::Degree ? "\xC2\xB0" : "");
    } else {
        text = FormatValue(value, Quantity::Length, units, 3);
        if (c.type == ConstraintType::Diameter || sketch::IsDiametric(sk, c)) text = "\xC3\x98" + text;   // Ø
        if (c.type == ConstraintType::Radius) text = "R" + text;
    }
    // A dimension driven by an expression shows that it is a formula.
    bool formula = false;
    for (char ch : c.parameter.expression) formula = formula || std::isalpha(static_cast<unsigned char>(ch)) || ch == '*' || ch == '/';
    if (formula && !c.reference) text = "fx: " + text;
    if (c.reference) text = "(" + text + ")";
    return text;
}

} // namespace

std::optional<Vec2> DimensionAnchor(const sketch::Sketch& sk, const sketch::Constraint& c) {
    switch (c.type) {
        case ConstraintType::Radius:
        case ConstraintType::Diameter: {
            if (const sketch::Circle* ci = sk.FindCircle(c.refs[0])) return sk.PositionOf(ci->center);
            if (const sketch::Arc* ar = sk.FindArc(c.refs[0])) return sk.PositionOf(ar->center);
            return std::nullopt;
        }
        case ConstraintType::Angle: {
            const auto parts = AngleParts(sk, c);
            if (!parts) return std::nullopt;
            return std::get<0>(*parts);
        }
        default: {
            const auto ends = LinearEnds(sk, c);
            if (!ends) return std::nullopt;
            return (ends->first + ends->second) * 0.5;
        }
    }
}

std::optional<double> MeasureDimension(const sketch::Sketch& sk, const sketch::Constraint& c) {
    switch (c.type) {
        case ConstraintType::Radius:   return sk.RadiusOf(c.refs[0]);
        case ConstraintType::Diameter: return 2.0 * sk.RadiusOf(c.refs[0]);
        case ConstraintType::Angle: {
            const sketch::Line* l1 = sk.FindLine(c.refs[0]);
            const sketch::Line* l2 = sk.FindLine(c.refs[1]);
            if (!l1 || !l2) return std::nullopt;
            const Vec2 d1 = sk.PositionOf(l1->end) - sk.PositionOf(l1->start);
            const Vec2 d2 = sk.PositionOf(l2->end) - sk.PositionOf(l2->start);
            return std::atan2(d1.Cross(d2), d1.Dot(d2));
        }
        case ConstraintType::DistanceX: {
            const auto ends = LinearEnds(sk, c);
            if (!ends) return std::nullopt;
            return std::abs(ends->second.x - ends->first.x);
        }
        case ConstraintType::DistanceY: {
            const auto ends = LinearEnds(sk, c);
            if (!ends) return std::nullopt;
            return std::abs(ends->second.y - ends->first.y);
        }
        default: {
            const auto ends = LinearEnds(sk, c);
            if (!ends) return std::nullopt;
            return (ends->second - ends->first).Length();
        }
    }
}

std::optional<DimensionLayout> LayoutDimension(const sketch::Sketch& sk, const sketch::Constraint& c,
                                               const UnitSystem& units) {
    DimensionLayout out;
    const std::optional<double> measured = MeasureDimension(sk, c);
    if (!measured) return std::nullopt;
    out.text = Label(sk, c, c.reference ? *measured : c.parameter.value, units);
    if (!c.reference) out.name = c.parameter.name;

    switch (c.type) {
        case ConstraintType::Radius:
        case ConstraintType::Diameter: {
            const Vec2 center = *DimensionAnchor(sk, c);
            const double r = sk.RadiusOf(c.refs[0]);
            Vec2 offset = c.labelOffset;
            if (offset.Length() < 1e-9) offset = Vec2{1, 1} * (r * 0.9);
            const Vec2 u = offset.Normalized();
            const Vec2 onCircle = center + u * r;
            const Vec2 label = center + offset;
            const Vec2 start = c.type == ConstraintType::Diameter ? center - u * r : center;
            out.lines.push_back({start, offset.Length() > r ? label : onCircle});
            out.arrows.push_back({onCircle, u});
            if (c.type == ConstraintType::Diameter) out.arrows.push_back({center - u * r, u * -1.0});
            out.label = label;
            out.anchor = center;
            return out;
        }
        case ConstraintType::Angle: {
            const auto parts = AngleParts(sk, c);
            if (!parts) return std::nullopt;
            const auto [vertex, u1, u2] = *parts;
            Vec2 offset = c.labelOffset;
            const Vec2 bisector = (u1 + u2).Length() > 1e-9 ? (u1 + u2).Normalized() : u1.Perp();
            if (offset.Length() < 1e-9) offset = bisector * 15.0;
            const double radius = offset.Length();
            double a1 = std::atan2(u1.y, u1.x);
            double a2 = std::atan2(u2.y, u2.x);
            double sweep = a2 - a1;
            while (sweep > kPi) sweep -= 2 * kPi;
            while (sweep < -kPi) sweep += 2 * kPi;
            constexpr int kSteps = 32;
            for (int i = 0; i <= kSteps; ++i) {
                const double a = a1 + sweep * i / kSteps;
                out.arc.push_back(vertex + Vec2{std::cos(a), std::sin(a)} * radius);
            }
            out.arrows.push_back({out.arc.back(), (out.arc.back() - out.arc[out.arc.size() - 2]).Normalized()});
            out.arrows.push_back({out.arc.front(), (out.arc.front() - out.arc[1]).Normalized()});
            out.label = vertex + offset;
            out.anchor = vertex;
            return out;
        }
        default: {
            const auto ends = LinearEnds(sk, c);
            if (!ends) return std::nullopt;
            const Vec2 a = ends->first, b = ends->second;
            const Vec2 mid = (a + b) * 0.5;
            Vec2 dir = c.type == ConstraintType::DistanceX ? Vec2{1, 0}
                     : c.type == ConstraintType::DistanceY ? Vec2{0, 1}
                                                           : (b - a).Normalized();
            if (dir.Length() < 1e-9) dir = {1, 0};
            const Vec2 n = dir.Perp();
            Vec2 offset = c.labelOffset;
            if (offset.Length() < 1e-9) offset = n * 10.0;

            // The dimension line runs through mid + offset, parallel to dir.
            const Vec2 through = mid + offset;
            const auto foot = [&](Vec2 p) { return through + dir * (p - through).Dot(dir); };
            const Vec2 fa = foot(a), fb = foot(b);
            const auto extension = [&](Vec2 from, Vec2 to) {
                const Vec2 d = to - from;
                const double len = d.Length();
                if (len < 1e-9) return;
                const Vec2 u = d / len;
                out.lines.push_back({from + u * std::min(1.0, len * 0.1), to + u * 2.0});
            };
            extension(a, fa);
            extension(b, fb);
            out.lines.push_back({fa, fb});
            // Text dragged out past an extension line: the dimension line
            // carries on to it (as in Inventor), so it never floats alone.
            const Vec2 span = fb - fa;
            const double len2 = span.Dot(span);
            if (len2 > 1e-18) {
                const double t = (through - fa).Dot(span) / len2;
                if (t < 0) out.lines.push_back({through, fa});
                else if (t > 1) out.lines.push_back({fb, through});
            }
            const Vec2 along = (fb - fa).Length() > 1e-9 ? (fb - fa).Normalized() : dir;
            out.arrows.push_back({fb, along});
            out.arrows.push_back({fa, along * -1.0});
            out.label = through;
            out.anchor = mid;
            return out;
        }
    }
}

} // namespace bz::app
