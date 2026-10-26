#include "sheetmetal/SheetPart.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace bz::sheet {
namespace {

constexpr double kEps = 1e-9;

double Wrap2Pi(double a) {
    while (a < 0.0) a += 2.0 * kPi;
    while (a >= 2.0 * kPi) a -= 2.0 * kPi;
    return a;
}

double AngleOf(Vec2 v) { return std::atan2(v.y, v.x); }

/// Points along a segment, for bounds and checks: the ends, the middle and,
/// for arcs, a few more.
std::vector<Vec2> Samples(const Segment& s, int perArc = 32) {
    if (!s.arc) return {s.a, s.Middle(), s.b};
    std::vector<Vec2> out;
    const double r = s.Radius();
    const double a0 = AngleOf(s.a - s.center);
    const double sweep = s.Sweep();
    for (int i = 0; i <= perArc; ++i) {
        const double t = a0 + sweep * i / perArc;
        out.push_back(s.center + Vec2{std::cos(t), std::sin(t)} * r);
    }
    return out;
}

double PointSegmentDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double len2 = d.Dot(d);
    const double t = len2 > 0.0 ? std::clamp((p - a).Dot(d) / len2, 0.0, 1.0) : 0.0;
    return (p - (a + d * t)).Length();
}

/// Where two lines (point + direction) cross; false when parallel.
bool Intersect(Vec2 p, Vec2 d, Vec2 q, Vec2 e, Vec2& out) {
    const double den = d.Cross(e);
    if (std::abs(den) < 1e-12) return false;
    const double t = (q - p).Cross(e) / den;
    out = p + d * t;
    return true;
}

Vec3 Unit(Vec3 v) { return v.Normalized(); }

/// `v` turned by `angle` around the unit direction `axis` (Rodrigues).
Vec3 Turn(Vec3 v, Vec3 axis, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return v * c + axis.Cross(v) * s + axis * (axis.Dot(v) * (1.0 - c));
}

std::string Num(double v) {
    std::string s = std::format("{:.2f}", v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

} // namespace

// --- Segment ------------------------------------------------------------------

double Segment::Sweep() const {
    if (!arc) return 0.0;
    if (full) return ccw ? 2.0 * kPi : -2.0 * kPi;
    const double a0 = AngleOf(a - center), a1 = AngleOf(b - center);
    if (ccw) {
        double d = Wrap2Pi(a1 - a0);
        if (d < kEps) d = 2.0 * kPi;
        return d;
    }
    double d = Wrap2Pi(a0 - a1);
    if (d < kEps) d = 2.0 * kPi;
    return -d;
}

double Segment::Length() const { return arc ? std::abs(Sweep()) * Radius() : (b - a).Length(); }

Vec2 Segment::Middle() const {
    if (!arc) return (a + b) * 0.5;
    const double t = AngleOf(a - center) + Sweep() / 2.0;
    return center + Vec2{std::cos(t), std::sin(t)} * Radius();
}

Segment Segment::Reversed() const {
    Segment r = *this;
    std::swap(r.a, r.b);
    r.ccw = !ccw;
    return r;
}

// --- Loops and regions ----------------------------------------------------------

double SignedArea(const Loop& loop) {
    // Green: area = 1/2 * closed integral of (x dy - y dx).
    double twice = 0.0;
    for (const Segment& s : loop) {
        if (!s.arc) {
            twice += s.a.Cross(s.b);
            continue;
        }
        const double r = s.Radius();
        const double t0 = AngleOf(s.a - s.center);
        const double t1 = t0 + s.Sweep();
        twice += r * s.center.x * (std::sin(t1) - std::sin(t0)) - r * s.center.y * (std::cos(t1) - std::cos(t0)) + r * r * (t1 - t0);
    }
    return twice / 2.0;
}

Loop Reversed(const Loop& loop) {
    Loop out;
    out.reserve(loop.size());
    for (auto it = loop.rbegin(); it != loop.rend(); ++it) out.push_back(it->Reversed());
    return out;
}

void Region::Orient() {
    if (SignedArea(outer) < 0.0) outer = Reversed(outer);
    for (Loop& h : holes)
        if (SignedArea(h) > 0.0) h = Reversed(h);
}

double Region::Area() const {
    double a = std::abs(SignedArea(outer));
    for (const Loop& h : holes) a -= std::abs(SignedArea(h));
    return a;
}

Box2 Region::Bounds() const {
    Box2 box;
    for (const Segment& s : outer)
        for (Vec2 p : Samples(s, 64)) box.Include(p);
    return box;
}

// --- Solve ------------------------------------------------------------------------

namespace {

/// A closed loop as a polygon of its sample points.
std::vector<Vec2> Polygon(const Loop& loop) {
    std::vector<Vec2> out;
    for (const Segment& s : loop) {
        std::vector<Vec2> pts = Samples(s, 48);
        if (!out.empty() && !pts.empty()) pts.erase(pts.begin());
        out.insert(out.end(), pts.begin(), pts.end());
    }
    return out;
}

bool Inside(const std::vector<Vec2>& poly, Vec2 p) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[j], b = poly[i];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

/// Segments through `points`, skipping the ones of no length.
void AddPath(Loop& out, const std::vector<Vec2>& points) {
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
        if ((points[i + 1] - points[i]).Length() > 1e-9) out.push_back(Segment::Line(points[i], points[i + 1]));
}

} // namespace

Expected<Layout> Solve(const Part& part, std::size_t* failed) {
    Layout out;
    out.base = part.base;
    out.base.Orient();
    const Loop& loop = out.base.outer;
    const std::size_t n = loop.size();
    const Rules& rules = part.rules;
    const Relief relief = Relief::For(rules);
    const auto fail = [&](std::size_t j, std::string text) {
        if (failed) *failed = j;
        return Expected<Layout>(Error{std::move(text)});
    };

    if (rules.thickness <= 0.0) return fail(0, "Толщина листа должна быть больше нуля");
    if (rules.radius < 0.0) return fail(0, "Радиус гиба не может быть отрицательным");
    if (rules.kFactor < 0.0 || rules.kFactor > 1.0) return fail(0, "K-фактор должен быть от 0 до 1");
    if (n < 2) return fail(0, "У листа нет замкнутого контура");

    std::vector<int> flangeAt(n, -1);
    out.flanges.resize(part.flanges.size());
    for (std::size_t j = 0; j < part.flanges.size(); ++j) {
        const Flange& f = part.flanges[j];
        const std::string who = std::format("Отбортовка {}", j + 1);
        if (!(f.angle > 1e-6 && f.angle < kPi - 1e-6))
            return fail(j, who + ": угол должен быть больше 0° и меньше 180°");
        BentFlange& b = out.flanges[j];
        b.parent = f.parent;
        b.onEdge = f.onEdge;
        b.angle = f.angle;
        b.down = f.down;
        b.setback = OuterSetback(f.angle, rules.radius, rules.thickness);
        b.straight = f.height - b.setback;
        b.allowance = BendAllowance(f.angle, rules.radius, rules.kFactor, rules.thickness);
        if (b.straight <= 1e-6)
            return fail(j, std::format("{}: высота {} мм меньше зоны сгиба, нужно больше {} мм", who, Num(f.height), Num(b.setback)));
        if (f.parent >= 0) {   // on an edge of an earlier flange
            if (f.parent >= static_cast<int>(j)) return fail(j, who + ": полка, на которой она стоит, должна идти раньше");
            BentFlange& under = out.flanges[static_cast<std::size_t>(f.parent)];
            if (under.onEdge != FlangeEdge::Far && under.parent >= 0)
                return fail(j, std::format("{}: полка {} сама стоит на боковом ребре — на ней пока ничего не ставится", who, f.parent + 1));
            int& slot = f.onEdge == FlangeEdge::Far ? under.child : f.onEdge == FlangeEdge::Start ? under.sideStart : under.sideEnd;
            if (slot >= 0) return fail(j, std::format("{}: на этом ребре полки {} уже есть отбортовка {}", who, f.parent + 1, slot + 1));
            slot = static_cast<int>(j);
            continue;
        }
        // The nearest straight edge of the outline.
        double best = std::numeric_limits<double>::max();
        std::size_t edge = n;
        for (std::size_t i = 0; i < n; ++i) {
            if (loop[i].arc) continue;
            const double d = PointSegmentDistance(f.edgePoint, loop[i].a, loop[i].b);
            if (d < best) {
                best = d;
                edge = i;
            }
        }
        if (edge == n) return fail(j, who + ": у листа нет прямых рёбер");
        if (flangeAt[edge] >= 0)
            return fail(j, std::format("{}: на этом ребре уже есть отбортовка {}", who, flangeAt[edge] + 1));
        flangeAt[edge] = static_cast<int>(j);
        const Vec2 d = (loop[edge].b - loop[edge].a).Normalized();
        const double length = (loop[edge].b - loop[edge].a).Length();
        b.edge = edge;
        b.out = {d.y, -d.x};   // outline runs counter-clockwise: the outside is on the right
        b.edgeMiddle = loop[edge].Middle();
        b.u1 = f.centered ? (length - f.width) / 2.0 : f.offsetStart;
        b.u2 = f.centered ? (length + f.width) / 2.0 : length - f.offsetEnd;
        if (b.u1 < -1e-9 || b.u2 > length + 1e-9 || b.u2 - b.u1 < 1e-6)
            return fail(j, std::format("{}: не помещается на ребро длиной {} мм", who, Num(length)));
        b.u1 = std::max(b.u1, 0.0), b.u2 = std::min(b.u2, length);
        // A narrower flange needs room for its reliefs.
        if (b.u1 > 1e-9 && b.u1 < relief.width - 1e-9)
            return fail(j, std::format("{}: отступ от начала ребра меньше рельефа ({} мм) — сделайте 0 или не меньше {}", who,
                                       Num(relief.width), Num(relief.width)));
        if (b.u2 < length - 1e-9 && b.u2 > length - relief.width + 1e-9)
            return fail(j, std::format("{}: отступ от конца ребра меньше рельефа ({} мм) — сделайте 0 или не меньше {}", who,
                                       Num(relief.width), Num(relief.width)));
    }
    // A flange on the far edge takes its bend out of the flange under it.
    for (std::size_t j = 0; j < out.flanges.size(); ++j) {
        const BentFlange& b = out.flanges[j];
        if (b.parent < 0 || b.onEdge != FlangeEdge::Far) continue;
        BentFlange& under = out.flanges[static_cast<std::size_t>(b.parent)];
        under.straight -= b.setback;
        if (under.straight <= 1e-6)
            return fail(j, std::format("Отбортовка {}: полка {} для неё слишком низкая — нужна высота больше {} мм", j + 1, b.parent + 1,
                                       Num(under.setback + b.setback)));
    }

    // The base: flanged edges move in by their setback where the flange
    // reaches the edge's end; the neighbours end where they meet them.
    const auto edgeLength = [&](std::size_t i) { return (loop[i].b - loop[i].a).Length(); };
    const auto reachesStart = [&](std::size_t i) { return flangeAt[i] >= 0 && out.flanges[static_cast<std::size_t>(flangeAt[i])].u1 < 1e-9; };
    const auto reachesEnd = [&](std::size_t i) {
        return flangeAt[i] >= 0 && out.flanges[static_cast<std::size_t>(flangeAt[i])].u2 > edgeLength(i) - 1e-9;
    };
    const auto lineOf = [&](std::size_t i, bool shifted, Vec2& p, Vec2& d) {
        d = (loop[i].b - loop[i].a).Normalized();
        p = loop[i].a;
        if (shifted) {
            const BentFlange& b = out.flanges[static_cast<std::size_t>(flangeAt[i])];
            p = p - b.out * b.setback;
        }
    };
    Loop moved = loop;   // the outline with the corners moved
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t next = (k + 1) % n;
        const bool endShifted = reachesEnd(k), startShifted = reachesStart(next);
        if (!endShifted && !startShifted) continue;
        const std::size_t j = static_cast<std::size_t>(endShifted ? flangeAt[k] : flangeAt[next]);
        if (loop[k].arc || loop[next].arc)
            return fail(j, std::format("Отбортовка {}: рядом с ребром дуга — пока отбортовка до конца ребра ставится только между прямыми; "
                                       "или отступите от конца",
                                       j + 1));
        Vec2 p, d, q, e, x;
        lineOf(k, endShifted, p, d);
        lineOf(next, startShifted, q, e);
        if (!Intersect(p, d, q, e, x))
            return fail(j, std::format("Отбортовка {}: соседнее ребро продолжает это — разбейте контур иначе", j + 1));
        moved[k].b = x;
        moved[next].a = x;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (loop[i].arc) continue;
        const Vec2 was = loop[i].b - loop[i].a;
        const Vec2 now = moved[i].b - moved[i].a;
        if (now.Dot(was) <= 1e-9 * was.Dot(was) + 1e-12) {
            const std::size_t j = static_cast<std::size_t>(flangeAt[i] >= 0 ? flangeAt[i] : std::max(flangeAt[(i + n - 1) % n], flangeAt[(i + 1) % n]));
            return fail(j, std::format("Отбортовка {}: сторона листа слишком короткая для такого сгиба", j + 1));
        }
    }
    // The new outline: flanged edges get the bend line and, where the flange
    // does not reach the edge's end, a relief cut beside it.
    Loop outline;
    for (std::size_t i = 0; i < n; ++i) {
        if (flangeAt[i] < 0) {
            outline.push_back(moved[i]);
            continue;
        }
        BentFlange& b = out.flanges[static_cast<std::size_t>(flangeAt[i])];
        const Vec2 d = (loop[i].b - loop[i].a).Normalized();
        const auto onEdge = [&](double u, double in) { return loop[i].a + d * u - b.out * in; };
        const double deep = b.setback + relief.depth;
        std::vector<Vec2> path{moved[i].a};
        const bool fromStart = reachesStart(i), toEnd = reachesEnd(i);
        if (!fromStart) {
            path.push_back(onEdge(b.u1 - relief.width, 0));
            path.push_back(onEdge(b.u1 - relief.width, deep));
            path.push_back(onEdge(b.u1, deep));
            path.push_back(onEdge(b.u1, b.setback));
        }
        b.a = fromStart ? moved[i].a : onEdge(b.u1, b.setback);
        b.b = toEnd ? moved[i].b : onEdge(b.u2, b.setback);
        AddPath(outline, path);
        b.bendSegment = outline.size();
        outline.push_back(Segment::Line(b.a, b.b));
        path = {b.b};
        if (!toEnd) {
            path.push_back(onEdge(b.u2, deep));
            path.push_back(onEdge(b.u2 + relief.width, deep));
            path.push_back(onEdge(b.u2 + relief.width, 0));
            path.push_back(moved[i].b);
        }
        AddPath(outline, path);
        if ((b.b - b.a).Dot(d) <= 1e-9) return fail(static_cast<std::size_t>(flangeAt[i]), std::format("Отбортовка {}: сторона листа слишком короткая", flangeAt[i] + 1));
    }
    // Holes must stay out of the bends and reliefs.
    for (const BentFlange& b : out.flanges) {
        if (b.parent >= 0) continue;
        const Vec2 d = (loop[b.edge].b - loop[b.edge].a).Normalized();
        const Vec2 origin = loop[b.edge].a;
        for (const Loop& hole : out.base.holes)
            for (const Segment& sg : hole)
                for (Vec2 p : Samples(sg)) {
                    const double u = (p - origin).Dot(d), in = -(p - origin).Dot(b.out);
                    if (u > b.u1 - relief.width - 1e-6 && u < b.u2 + relief.width + 1e-6 && in < b.setback + relief.depth + 1e-6) {
                        const std::size_t j = static_cast<std::size_t>(flangeAt[b.edge]);
                        return fail(j, std::format("Отбортовка {}: отверстие заходит в зону сгиба", j + 1));
                    }
                }
    }
    out.base.outer = std::move(outline);

    // Where every bend and strip is, in part coordinates, flanges after the ones they stand on.
    const double s = rules.thickness, r = rules.radius;
    const double z0 = part.reversed ? -s : 0.0;
    for (BentFlange& b : out.flanges) {
        if (b.parent < 0) {
            b.start = {b.a.x, b.a.y, z0};
            b.along = Unit(Vec3{b.b.x - b.a.x, b.b.y - b.a.y, 0});
            b.outward = {b.out.x, b.out.y, 0};
            b.normal = {0, 0, 1};
            b.width = (b.b - b.a).Length();
        } else {
            const BentFlange& under = out.flanges[static_cast<std::size_t>(b.parent)];
            switch (b.onEdge) {
                case FlangeEdge::Far:
                    b.start = under.stripStart + under.stripOut * under.straight;
                    b.along = under.along;
                    b.outward = under.stripOut;
                    b.width = under.width;
                    break;
                case FlangeEdge::Start:   // the side edge at the strip's start: bends from the edge itself
                    b.start = under.stripStart;
                    b.along = under.stripOut;
                    b.outward = under.along * -1.0;
                    b.width = under.straight;
                    break;
                case FlangeEdge::End:
                    b.start = under.stripStart + under.along * under.width;
                    b.along = under.stripOut;
                    b.outward = under.along;
                    b.width = under.straight;
                    break;
            }
            b.normal = under.stripNormal;
        }
        // Up: the inner side is the face's top, the axis R above it; down: R below its bottom.
        if (!b.down) {
            b.axisPoint = b.start + b.normal * (s + r);
            b.axisDir = Unit(b.outward.Cross(b.normal));
        } else {
            b.axisPoint = b.start - b.normal * r;
            b.axisDir = Unit(b.normal.Cross(b.outward));
        }
        b.stripStart = b.axisPoint + Turn(b.start - b.axisPoint, b.axisDir, b.angle);
        b.stripOut = Unit(Turn(b.outward, b.axisDir, b.angle));
        b.stripNormal = Unit(Turn(b.normal, b.axisDir, b.angle));
    }

    // Cuts: inside one face, clear of its edges.
    const std::vector<Vec2> basePolygon = Polygon(out.base.outer);
    for (std::size_t c = 0; c < part.cuts.size(); ++c) {
        const Cut& cut = part.cuts[c];
        const std::size_t at = part.flanges.size() + c;
        const std::string who = std::format("Вырез {}", c + 1);
        if (cut.loop.empty()) return fail(at, who + ": пустой контур");
        Loop loopCw = cut.loop;
        if (SignedArea(loopCw) > 0) loopCw = Reversed(loopCw);
        std::vector<Vec2> pts;
        for (const Segment& sg : loopCw)
            for (Vec2 p : Samples(sg)) pts.push_back(p);
        if (cut.face < 0) {
            for (Vec2 p : pts)
                if (!Inside(basePolygon, p))
                    return fail(at, who + ": выходит за плоскую часть листа (через край и на сгиб — пока нельзя)");
            const std::vector<Vec2> mine = Polygon(loopCw);
            for (Vec2 p : basePolygon)
                if (Inside(mine, p)) return fail(at, who + ": задевает край листа");
            out.base.holes.push_back(std::move(loopCw));
            continue;
        }
        if (cut.face >= static_cast<int>(out.flanges.size())) return fail(at, who + ": полка не найдена");
        const BentFlange& b = out.flanges[static_cast<std::size_t>(cut.face)];
        for (Vec2 p : pts)
            if (p.x < 1e-6 || p.x > b.width - 1e-6 || p.y < 1e-6 || p.y > b.straight - 1e-6)
                return fail(at, std::format("{}: выходит за плоскую часть полки {} (на сгиб и за край — пока нельзя)", who, cut.face + 1));
        out.stripCuts.push_back({cut.face, std::move(loopCw)});
    }
    return out;
}

// --- Flat pattern ------------------------------------------------------------------

FlatPattern Flatten(const Part& part, const Layout& layout) {
    FlatPattern fp;
    fp.thickness = part.rules.thickness;
    fp.strips.resize(layout.flanges.size());
    const auto flange = [&](int k) -> const BentFlange* { return k >= 0 ? &layout.flanges[static_cast<std::size_t>(k)] : nullptr; };
    const auto bend = [&](Vec2 a, Vec2 b, const BentFlange& k) { fp.bends.push_back({a, b, k.angle, k.down, part.rules.radius}); };

    std::vector<const BentFlange*> at(layout.base.outer.size(), nullptr);
    for (const BentFlange& b : layout.flanges)
        if (b.parent < 0 && b.bendSegment < at.size()) at[b.bendSegment] = &b;
    for (std::size_t i = 0; i < layout.base.outer.size(); ++i) {
        const Segment& s = layout.base.outer[i];
        const BentFlange* b = at[i];
        if (!b) {
            fp.outline.outer.push_back(s);
            continue;
        }
        const Vec2 d = (b->b - b->a).Normalized(), n = b->out;
        // The flange and the ones on its far edge lie flat one after another;
        // flanges on their side edges stick out sideways.
        std::vector<Vec2> left{b->a}, right;   // up the start side, down the end side
        double length = 0.0;
        for (const BentFlange* k = b; k; k = flange(k->child)) {
            const Vec2 mid = n * (length + k->allowance / 2.0);
            bend(b->a + mid, b->b + mid, *k);
            const double from = length + k->allowance, to = from + k->straight;
            const std::size_t index = static_cast<std::size_t>(k - layout.flanges.data());
            fp.strips[index] = {b->a + n * from, d, n};
            if (const BentFlange* side = flange(k->sideStart)) {
                const double len = side->allowance + side->straight;
                const Vec2 p0 = b->a + n * from, p1 = b->a + n * to;
                left.insert(left.end(), {p0, p0 - d * len, p1 - d * len, p1});
                bend(p0 - d * (side->allowance / 2), p1 - d * (side->allowance / 2), *side);
                fp.strips[static_cast<std::size_t>(k->sideStart)] = {p0 - d * side->allowance, n, d * -1.0};
            }
            if (const BentFlange* side = flange(k->sideEnd)) {
                const double len = side->allowance + side->straight;
                const Vec2 p0 = b->b + n * from, p1 = b->b + n * to;
                right.insert(right.begin(), {p1, p1 + d * len, p0 + d * len, p0});
                bend(p0 + d * (side->allowance / 2), p1 + d * (side->allowance / 2), *side);
                fp.strips[static_cast<std::size_t>(k->sideEnd)] = {p0 + d * side->allowance, n, d};
            }
            length = to;
        }
        left.push_back(b->a + n * length);
        left.push_back(b->b + n * length);
        left.insert(left.end(), right.begin(), right.end());
        left.push_back(b->b);
        AddPath(fp.outline.outer, left);
    }
    fp.outline.holes = layout.base.holes;
    for (const Cut& c : layout.stripCuts) {
        const FlatFrame& f = fp.strips[static_cast<std::size_t>(c.face)];
        Loop mapped;
        const bool flips = f.x.Cross(f.y) < 0;
        for (const Segment& sg : c.loop) {
            Segment m = sg;
            m.a = f.Map(sg.a), m.b = f.Map(sg.b), m.center = f.Map(sg.center);
            if (flips) m.ccw = !m.ccw;
            mapped.push_back(m);
        }
        fp.outline.holes.push_back(std::move(mapped));
    }
    fp.outline.Orient();
    return fp;
}

std::string BendNote(const FlatPattern::Bend& bend) {
    return std::format("{} {}° R{}", bend.down ? "ВНИЗ" : "ВВЕРХ", Num(bend.angle * 180.0 / kPi), Num(bend.radius));
}

} // namespace bz::sheet
