#include "sketch/EquationSystem.h"

#include <cmath>
#include <initializer_list>
#include <utility>

namespace bz::sketch::detail {
namespace {

constexpr double kTiny = 1e-12;

double SignOf(double v) { return v < 0.0 ? -1.0 : 1.0; }

double WrapAngle(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a <= -kPi) a += 2.0 * kPi;
    return a;
}

/// Equation sum(k * x[i]) - value = 0 from (coefficient, index) pairs.
Equation MakeLinear(std::initializer_list<std::pair<double, int>> terms, double value, ObjectId source) {
    Equation eq;
    eq.kind   = EqKind::Linear;
    eq.value  = value;
    eq.source = source;
    for (const auto& [coef, index] : terms) {
        eq.k[static_cast<std::size_t>(eq.count)] = coef;
        eq.v[static_cast<std::size_t>(eq.count)] = index;
        ++eq.count;
    }
    return eq;
}

Equation MakeEq(EqKind kind, std::initializer_list<int> unknowns, double value, ObjectId source) {
    Equation eq;
    eq.kind   = kind;
    eq.value  = value;
    eq.source = source;
    for (int index : unknowns) eq.v[static_cast<std::size_t>(eq.count++)] = index;
    return eq;
}

} // namespace

EquationSystem::EquationSystem(const Sketch& sketch, ObjectId skip, const Constraint* extra) {
    // Unknowns: x, y of every point; radius of every circle and arc.
    for (const Point& p : sketch.points) {
        indexOf[p.id] = static_cast<int>(x.size());
        x.push_back(p.pos.x);
        x.push_back(p.pos.y);
        // The origin and projected model geometry are held where they are.
        isFree.push_back(!p.isOrigin && !p.projected);
        isFree.push_back(!p.isOrigin && !p.projected);
    }
    for (const Circle& c : sketch.circles) {
        indexOf[c.id] = static_cast<int>(x.size());
        x.push_back(c.radius);
        isFree.push_back(!c.projected);
    }
    for (const Arc& a : sketch.arcs) {
        indexOf[a.id] = static_cast<int>(x.size());
        x.push_back(sketch.RadiusOf(a.id));
        isFree.push_back(!a.projected);
        // Both ends stay on the arc's circle.
        const int c = PointIndex(a.center);
        const int r = RadiusIndex(a.id);
        Add(MakeEq(EqKind::PointDistanceVar, {PointIndex(a.start), PointIndex(a.start) + 1, c, c + 1, r}, 0.0, {}));
        Add(MakeEq(EqKind::PointDistanceVar, {PointIndex(a.end), PointIndex(a.end) + 1, c, c + 1, r}, 0.0, {}));
    }

    for (const Constraint& c : sketch.constraints)
        if (c.id != skip) AddConstraint(sketch, c);
    if (extra) AddConstraint(sketch, *extra);
}

int EquationSystem::PointIndex(ObjectId id) const {
    const auto it = indexOf.find(id);
    return it != indexOf.end() ? it->second : -1;
}

int EquationSystem::RadiusIndex(ObjectId id) const { return PointIndex(id); }

void EquationSystem::AddConstraint(const Sketch& sk, const Constraint& c) {
    if (c.reference) return;   // driven dimensions only measure

    const ObjectId a = c.refs[0];
    const ObjectId b = c.refs[1];
    const ObjectId src = c.id;
    const auto P = [&](ObjectId id) { return PointIndex(id); };
    const auto pos = [&](int i) { return Vec2{x[static_cast<std::size_t>(i)], x[static_cast<std::size_t>(i) + 1]}; };

    // Endpoints of a line as unknown indices; -1 when `id` is not a line.
    const auto lineEnds = [&](ObjectId id) -> std::pair<int, int> {
        const Line* l = sk.FindLine(id);
        return l ? std::pair{P(l->start), P(l->end)} : std::pair{-1, -1};
    };
    // Center and radius of a circle or arc.
    const auto circleParts = [&](ObjectId id) -> std::pair<int, int> {
        if (const Circle* ci = sk.FindCircle(id)) return {P(ci->center), RadiusIndex(id)};
        if (const Arc* ar = sk.FindArc(id)) return {P(ar->center), RadiusIndex(id)};
        return {-1, -1};
    };
    const bool aIsLine = sk.FindLine(a) != nullptr;
    const bool bIsLine = sk.FindLine(b) != nullptr;

    switch (c.type) {
        case ConstraintType::Coincident:
        case ConstraintType::Concentric: {
            const int p = c.type == ConstraintType::Concentric ? circleParts(a).first : P(a);
            const int q = c.type == ConstraintType::Concentric ? circleParts(b).first : P(b);
            if (p < 0 || q < 0) return;
            Add(MakeLinear({{1, p}, {-1, q}}, 0, src));
            Add(MakeLinear({{1, p + 1}, {-1, q + 1}}, 0, src));
            return;
        }
        case ConstraintType::Horizontal:
        case ConstraintType::Vertical: {
            const auto [s, e] = aIsLine ? lineEnds(a) : std::pair{P(a), P(b)};
            if (s < 0 || e < 0) return;
            const int axis = c.type == ConstraintType::Horizontal ? 1 : 0;
            Add(MakeLinear({{1, s + axis}, {-1, e + axis}}, 0, src));
            return;
        }
        case ConstraintType::Parallel:
        case ConstraintType::Perpendicular:
        case ConstraintType::Angle: {
            const auto [s1, e1] = lineEnds(a);
            const auto [s2, e2] = lineEnds(b);
            if (s1 < 0 || s2 < 0) return;
            const EqKind kind = c.type == ConstraintType::Parallel        ? EqKind::Parallel
                                : c.type == ConstraintType::Perpendicular ? EqKind::Perpendicular
                                                                          : EqKind::Angle;
            const double value = c.type == ConstraintType::Angle ? c.parameter.value : 0.0;
            Add(MakeEq(kind, {s1, s1 + 1, e1, e1 + 1, s2, s2 + 1, e2, e2 + 1}, value, src));
            return;
        }
        case ConstraintType::Equal: {
            if (aIsLine && bIsLine) {
                const auto [s1, e1] = lineEnds(a);
                const auto [s2, e2] = lineEnds(b);
                Add(MakeEq(EqKind::EqualLength, {s1, s1 + 1, e1, e1 + 1, s2, s2 + 1, e2, e2 + 1}, 0, src));
            } else {
                const int r1 = circleParts(a).second;
                const int r2 = circleParts(b).second;
                if (r1 < 0 || r2 < 0) return;
                Add(MakeLinear({{1, r1}, {-1, r2}}, 0, src));
            }
            return;
        }
        case ConstraintType::Fix: {
            const int p = P(a);
            if (p < 0) return;
            x[static_cast<std::size_t>(p)]     = c.fixedAt.x;
            x[static_cast<std::size_t>(p) + 1] = c.fixedAt.y;
            isFree[static_cast<std::size_t>(p)]     = false;
            isFree[static_cast<std::size_t>(p) + 1] = false;
            return;
        }
        case ConstraintType::PointOnCurve: {
            const int p = P(a);
            if (p < 0) return;
            if (bIsLine) {
                const auto [s, e] = lineEnds(b);
                Add(MakeEq(EqKind::PointLine, {p, p + 1, s, s + 1, e, e + 1}, 0, src));
            } else {
                const auto [center, r] = circleParts(b);
                if (center < 0) return;
                Add(MakeEq(EqKind::PointDistanceVar, {p, p + 1, center, center + 1, r}, 0, src));
            }
            return;
        }
        case ConstraintType::Midpoint: {
            const int p = P(a);
            const auto [s, e] = lineEnds(b);
            if (p < 0 || s < 0) return;
            Add(MakeLinear({{2, p}, {-1, s}, {-1, e}}, 0, src));
            Add(MakeLinear({{2, p + 1}, {-1, s + 1}, {-1, e + 1}}, 0, src));
            return;
        }
        case ConstraintType::Tangent: {
            if (aIsLine || bIsLine) {
                const auto [s, e] = lineEnds(aIsLine ? a : b);
                const auto [center, r] = circleParts(aIsLine ? b : a);
                if (s < 0 || center < 0) return;
                // Keep the circle on the side of the line where it is now.
                const Vec2 A = pos(s), B = pos(e), C = pos(center);
                Equation eq = MakeEq(EqKind::PointLineVar, {center, center + 1, s, s + 1, e, e + 1, r}, 0, src);
                eq.sign = SignOf((B - A).Cross(C - A));
                Add(eq);
            } else {
                const auto [c1, r1] = circleParts(a);
                const auto [c2, r2] = circleParts(b);
                if (c1 < 0 || c2 < 0) return;
                // External or internal tangency, whichever is closer to the current state.
                const double d  = (pos(c1) - pos(c2)).Length();
                const double R1 = x[static_cast<std::size_t>(r1)];
                const double R2 = x[static_cast<std::size_t>(r2)];
                Equation eq = MakeEq(EqKind::CircleTangent, {c1, c1 + 1, c2, c2 + 1, r1, r2}, 0, src);
                const bool internal = d < std::max(R1, R2);
                eq.k = internal ? (R1 >= R2 ? std::array<double, 4>{1, -1, 0, 0} : std::array<double, 4>{-1, 1, 0, 0})
                                : std::array<double, 4>{1, 1, 0, 0};
                Add(eq);
            }
            return;
        }
        case ConstraintType::Distance:
        case ConstraintType::Length: {
            const auto [p, q] = c.type == ConstraintType::Length ? lineEnds(a) : std::pair{P(a), P(b)};
            if (p < 0 || q < 0) return;
            Add(MakeEq(EqKind::PointDistance, {p, p + 1, q, q + 1}, c.parameter.value, src));
            return;
        }
        case ConstraintType::DistanceX:
        case ConstraintType::DistanceY: {
            const int p = P(a), q = P(b);
            if (p < 0 || q < 0) return;
            const int axis = c.type == ConstraintType::DistanceX ? 0 : 1;
            // Keep the current order of the two points along the axis.
            const double s = SignOf(x[static_cast<std::size_t>(q + axis)] - x[static_cast<std::size_t>(p + axis)]);
            Add(MakeLinear({{s, q + axis}, {-s, p + axis}}, c.parameter.value, src));
            return;
        }
        case ConstraintType::PointLineDistance: {
            const int p = P(a);
            const auto [s, e] = lineEnds(b);
            if (p < 0 || s < 0) return;
            Equation eq = MakeEq(EqKind::PointLine, {p, p + 1, s, s + 1, e, e + 1}, c.parameter.value, src);
            eq.sign = SignOf((pos(e) - pos(s)).Cross(pos(p) - pos(s)));
            Add(eq);
            return;
        }
        case ConstraintType::Radius:
        case ConstraintType::Diameter: {
            const int r = circleParts(a).second;
            if (r < 0) return;
            const double k = c.type == ConstraintType::Diameter ? 2.0 : 1.0;
            Add(MakeLinear({{k, r}}, c.parameter.value, src));
            return;
        }
    }
}

double EquationSystem::Residual(const Equation& eq, const std::vector<double>& xs) const {
    const auto X = [&](int slot) { return xs[static_cast<std::size_t>(eq.v[static_cast<std::size_t>(slot)])]; };
    const auto V = [&](int slot) { return Vec2{X(slot), X(slot + 1)}; };

    switch (eq.kind) {
        case EqKind::Linear: {
            double sum = 0.0;
            for (int i = 0; i < eq.count; ++i) sum += eq.k[static_cast<std::size_t>(i)] * X(i);
            return sum - eq.value;
        }
        case EqKind::PointDistance:
            return (V(0) - V(2)).Length() - eq.value;
        case EqKind::PointDistanceVar:
            return (V(0) - V(2)).Length() - X(4);
        case EqKind::PointLine:
        case EqKind::PointLineVar: {
            const Vec2 p = V(0), a = V(2), b = V(4);
            const double len = std::max((b - a).Length(), kTiny);
            const double dist = eq.sign * (b - a).Cross(p - a) / len;
            return dist - (eq.kind == EqKind::PointLineVar ? X(6) : eq.value);
        }
        case EqKind::Parallel:
        case EqKind::Perpendicular:
        case EqKind::EqualLength:
        case EqKind::Angle: {
            const Vec2 d1 = V(2) - V(0);
            const Vec2 d2 = V(6) - V(4);
            const double l1 = std::max(d1.Length(), kTiny);
            const double l2 = std::max(d2.Length(), kTiny);
            if (eq.kind == EqKind::Parallel) return d1.Cross(d2) / (l1 * l2);
            if (eq.kind == EqKind::Perpendicular) return d1.Dot(d2) / (l1 * l2);
            if (eq.kind == EqKind::EqualLength) return l1 - l2;
            return WrapAngle(std::atan2(d1.Cross(d2), d1.Dot(d2)) - eq.value);
        }
        case EqKind::CircleTangent:
            return (V(0) - V(2)).Length() - (eq.k[0] * X(4) + eq.k[1] * X(5));
    }
    return 0.0;
}

void EquationSystem::Apply(Sketch& sketch) const {
    for (Point& p : sketch.points) {
        const auto i = static_cast<std::size_t>(indexOf.at(p.id));
        p.pos = {x[i], x[i + 1]};
    }
    for (Circle& c : sketch.circles) c.radius = x[static_cast<std::size_t>(indexOf.at(c.id))];
}

void EquationSystem::Pin(ObjectId point, Vec2 target) {
    const int i = PointIndex(point);
    if (i < 0) return;
    const auto u = static_cast<std::size_t>(i);
    x[u] = target.x;
    x[u + 1] = target.y;
    isFree[u] = false;
    isFree[u + 1] = false;
}

std::vector<int> EquationSystem::UnknownsOf(const Sketch& sketch, ObjectId entity) const {
    std::vector<int> out;
    const auto addPoint = [&](ObjectId id) {
        const int i = PointIndex(id);
        if (i >= 0) {
            out.push_back(i);
            out.push_back(i + 1);
        }
    };
    if (sketch.FindPoint(entity)) addPoint(entity);
    if (const Line* l = sketch.FindLine(entity)) {
        addPoint(l->start);
        addPoint(l->end);
    }
    if (const Circle* c = sketch.FindCircle(entity)) {
        addPoint(c->center);
        out.push_back(RadiusIndex(entity));
    }
    if (const Arc* a = sketch.FindArc(entity)) {
        addPoint(a->center);
        addPoint(a->start);
        addPoint(a->end);
        out.push_back(RadiusIndex(entity));
    }
    return out;
}

} // namespace bz::sketch::detail
