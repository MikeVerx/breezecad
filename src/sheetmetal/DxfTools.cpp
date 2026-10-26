// Geometry of DXF entities for the editor: outlines, picking, moving,
// and the checks a blank needs before it goes to the laser.

#include "sheetmetal/DxfDrawing.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace bz::dxf {
namespace {

constexpr double kDeg = kPi / 180.0;

Vec2 Polar(Vec2 c, double r, double deg) { return c + Vec2{std::cos(deg * kDeg), std::sin(deg * kDeg)} * r; }

/// Counter-clockwise sweep of an arc, degrees, in (0, 360].
double Sweep(double startDeg, double endDeg) {
    double s = std::fmod(endDeg - startDeg, 360.0);
    if (s <= 1e-12) s += 360.0;
    return s;
}

/// The arc a polyline segment with `bulge` makes from `a` to `b`.
struct BulgeArc {
    Vec2   center;
    double radius = 0, startDeg = 0, sweepDeg = 0;   ///< sweep signed: negative clockwise
};
BulgeArc ArcOfBulge(Vec2 a, Vec2 b, double bulge) {
    BulgeArc arc;
    const double theta = 4.0 * std::atan(bulge);   // included angle, signed
    const Vec2 chord = b - a;
    const double c = chord.Length();
    arc.radius = c / (2.0 * std::sin(std::abs(theta) / 2.0));
    const Vec2 mid = (a + b) * 0.5;
    const double h = arc.radius * std::cos(theta / 2.0);   // centre from the chord's middle
    const Vec2 left = chord.Perp().Normalized();
    arc.center = mid + left * (bulge > 0 ? h : -h);
    arc.startDeg = std::atan2(a.y - arc.center.y, a.x - arc.center.x) / kDeg;
    arc.sweepDeg = theta / kDeg;
    return arc;
}

void AddArc(std::vector<Vec2>& out, Vec2 c, double r, double startDeg, double sweepDeg, double maxAngleDeg, bool skipFirst) {
    const int n = std::max(1, static_cast<int>(std::ceil(std::abs(sweepDeg) / maxAngleDeg)));
    for (int i = skipFirst ? 1 : 0; i <= n; ++i) out.push_back(Polar(c, r, startDeg + sweepDeg * i / n));
}

double PointSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double len2 = d.Dot(d);
    const double t = len2 > 0 ? std::clamp((p - a).Dot(d) / len2, 0.0, 1.0) : 0.0;
    return (p - (a + d * t)).Length();
}

/// Distance to an arc given by centre, radius, start and signed sweep (degrees).
double PointArc(Vec2 p, Vec2 c, double r, double startDeg, double sweepDeg) {
    const double ang = std::atan2(p.y - c.y, p.x - c.x) / kDeg;
    double rel = std::fmod((sweepDeg >= 0 ? ang - startDeg : startDeg - ang) + 720.0, 360.0);
    if (rel <= std::abs(sweepDeg)) return std::abs((p - c).Length() - r);
    const Vec2 a = Polar(c, r, startDeg), b = Polar(c, r, startDeg + sweepDeg);
    return std::min((p - a).Length(), (p - b).Length());
}

/// Corners of a text's box (width guessed from the number of letters).
std::vector<Vec2> TextBox(const Entity& e) {
    std::size_t letters = 0;
    for (const char ch : e.text)
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++letters;
    const double w = static_cast<double>(std::max<std::size_t>(letters, 1)) * e.height * 0.7, h = e.height;
    const double a = e.rotationDeg * kDeg;
    const Vec2 x{std::cos(a), std::sin(a)}, y = x.Perp();
    return {e.at, e.at + x * w, e.at + x * w + y * h, e.at + y * h};
}

} // namespace

std::vector<Vec2> Outline(const Entity& e, double maxAngleDeg) {
    std::vector<Vec2> out;
    switch (e.kind) {
        case Kind::Line: return {e.a, e.b};
        case Kind::Circle: AddArc(out, e.center, e.radius, 0, 360, maxAngleDeg, false); return out;
        case Kind::Arc: AddArc(out, e.center, e.radius, e.startDeg, Sweep(e.startDeg, e.endDeg), maxAngleDeg, false); return out;
        case Kind::Text: return TextBox(e);
        case Kind::Polyline: {
            const std::size_t n = e.points.size();
            if (n == 0) return out;
            out.push_back(e.points[0]);
            const std::size_t segments = e.closed ? n : n - 1;
            for (std::size_t i = 0; i < segments; ++i) {
                const Vec2 a = e.points[i], b = e.points[(i + 1) % n];
                const double bulge = i < e.bulges.size() ? e.bulges[i] : 0.0;
                if (std::abs(bulge) < 1e-12 || (b - a).Length() < 1e-12) {
                    out.push_back(b);
                    continue;
                }
                const BulgeArc arc = ArcOfBulge(a, b, bulge);
                AddArc(out, arc.center, arc.radius, arc.startDeg, arc.sweepDeg, maxAngleDeg, true);
                out.back() = b;   // exactly on the vertex
            }
            return out;
        }
    }
    return out;
}

bool Ends(const Entity& e, Vec2& start, Vec2& end) {
    switch (e.kind) {
        case Kind::Line: start = e.a, end = e.b; return true;
        case Kind::Arc: start = Polar(e.center, e.radius, e.startDeg), end = Polar(e.center, e.radius, e.endDeg); return true;
        case Kind::Polyline:
            if (e.closed || e.points.size() < 2) return false;
            start = e.points.front(), end = e.points.back();
            return true;
        default: return false;
    }
}

Box2 Bounds(const Entity& e) {
    Box2 box;
    if (e.kind == Kind::Circle) {
        box.Include(e.center, e.radius);
        return box;
    }
    for (Vec2 p : Outline(e, 2.0)) box.Include(p);
    return box;
}

Box2 Bounds(const Drawing& d, bool visibleOnly) {
    Box2 box;
    for (const Entity& e : d.entities) {
        if (visibleOnly)
            if (const Layer* l = d.FindLayer(e.layer); l && !l->visible) continue;
        const Box2 b = Bounds(e);
        if (!b.IsEmpty()) box.Include(b.min), box.Include(b.max);
    }
    return box;
}

double Distance(const Entity& e, Vec2 p) {
    switch (e.kind) {
        case Kind::Line: return PointSegment(p, e.a, e.b);
        case Kind::Circle: return std::abs((p - e.center).Length() - e.radius);
        case Kind::Arc: return PointArc(p, e.center, e.radius, e.startDeg, Sweep(e.startDeg, e.endDeg));
        case Kind::Text: {
            const std::vector<Vec2> box = TextBox(e);
            // Inside the box: 0.
            bool inside = true;
            for (std::size_t i = 0; i < 4; ++i)
                if ((box[(i + 1) % 4] - box[i]).Cross(p - box[i]) < 0) inside = false;
            if (inside) return 0.0;
            double best = 1e300;
            for (std::size_t i = 0; i < 4; ++i) best = std::min(best, PointSegment(p, box[i], box[(i + 1) % 4]));
            return best;
        }
        case Kind::Polyline: {
            double best = 1e300;
            const std::size_t n = e.points.size();
            if (n == 1) return (p - e.points[0]).Length();
            const std::size_t segments = e.closed ? n : n - 1;
            for (std::size_t i = 0; i < segments; ++i) {
                const Vec2 a = e.points[i], b = e.points[(i + 1) % n];
                const double bulge = i < e.bulges.size() ? e.bulges[i] : 0.0;
                if (std::abs(bulge) < 1e-12) best = std::min(best, PointSegment(p, a, b));
                else {
                    const BulgeArc arc = ArcOfBulge(a, b, bulge);
                    best = std::min(best, PointArc(p, arc.center, arc.radius, arc.startDeg, arc.sweepDeg));
                }
            }
            return best;
        }
    }
    return 1e300;
}

std::vector<Vec2> SnapPoints(const Entity& e) {
    switch (e.kind) {
        case Kind::Line: return {e.a, e.b, (e.a + e.b) * 0.5};
        case Kind::Circle: return {e.center, Polar(e.center, e.radius, 0), Polar(e.center, e.radius, 90), Polar(e.center, e.radius, 180),
                                   Polar(e.center, e.radius, 270)};
        case Kind::Arc: {
            const double s = Sweep(e.startDeg, e.endDeg);
            return {e.center, Polar(e.center, e.radius, e.startDeg), Polar(e.center, e.radius, e.endDeg),
                    Polar(e.center, e.radius, e.startDeg + s / 2)};
        }
        case Kind::Polyline: return e.points;
        case Kind::Text: return {e.at};
    }
    return {};
}

void Move(Entity& e, Vec2 by) {
    e.a = e.a + by, e.b = e.b + by, e.center = e.center + by, e.at = e.at + by;
    for (Vec2& p : e.points) p = p + by;
}

void Rotate(Entity& e, Vec2 around, double angleDeg) {
    const double a = angleDeg * kDeg, c = std::cos(a), s = std::sin(a);
    const auto turn = [&](Vec2 p) {
        const Vec2 d = p - around;
        return around + Vec2{d.x * c - d.y * s, d.x * s + d.y * c};
    };
    e.a = turn(e.a), e.b = turn(e.b), e.center = turn(e.center), e.at = turn(e.at);
    for (Vec2& p : e.points) p = turn(p);
    if (e.kind == Kind::Arc) e.startDeg += angleDeg, e.endDeg += angleDeg;
    if (e.kind == Kind::Text) e.rotationDeg += angleDeg;
}

void MirrorX(Entity& e, double x) {
    const auto flip = [x](Vec2 p) { return Vec2{2 * x - p.x, p.y}; };
    e.a = flip(e.a), e.b = flip(e.b), e.center = flip(e.center);
    for (Vec2& p : e.points) p = flip(p);
    for (double& b : e.bulges) b = -b;
    if (e.kind == Kind::Arc) {
        const double s = e.startDeg, t = e.endDeg;
        e.startDeg = 180.0 - t, e.endDeg = 180.0 - s;
    }
    if (e.kind == Kind::Text) {   // text stays readable: only its place moves
        const std::vector<Vec2> box = TextBox(e);
        e.at = flip(box[1]);
    }
}

Check CheckForCutting(const Drawing& d, std::string_view layer, double tolerance) {
    Check out;
    const auto wanted = [&](const Entity& e) { return e.kind != Kind::Text && (layer.empty() || e.layer == layer); };
    // Ends clustered on a grid of `tolerance`.
    struct Node {
        Vec2 at;
        int  degree = 0;
    };
    std::vector<Node> nodes;
    std::unordered_map<long long, std::vector<int>> grid;
    const auto key = [&](long long gx, long long gy) { return gx * 73856093LL ^ gy * 19349663LL; };
    const auto node = [&](Vec2 p) {
        const long long gx = static_cast<long long>(std::floor(p.x / tolerance)), gy = static_cast<long long>(std::floor(p.y / tolerance));
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy)
                if (const auto it = grid.find(key(gx + dx, gy + dy)); it != grid.end())
                    for (const int n : it->second)
                        if ((nodes[static_cast<std::size_t>(n)].at - p).Length() <= tolerance) return n;
        nodes.push_back({p, 0});
        const int n = static_cast<int>(nodes.size()) - 1;
        grid[key(gx, gy)].push_back(n);
        return n;
    };
    // Union-find over nodes for chains.
    std::vector<int> parent;
    const auto find = [&](int a) {
        while (parent[static_cast<std::size_t>(a)] != a) a = parent[static_cast<std::size_t>(a)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(a)])];
        return a;
    };
    std::vector<std::pair<int, int>> edges;
    for (const Entity& e : d.entities) {
        if (!wanted(e)) continue;
        if (e.kind == Kind::Circle) {
            ++out.closedContours;
            continue;
        }
        Vec2 a, b;
        if (!Ends(e, a, b)) {
            if (e.kind == Kind::Polyline && e.closed) ++out.closedContours;
            continue;
        }
        const int na = node(a), nb = node(b);
        if (na == nb && e.kind == Kind::Polyline) {   // a polyline that comes back to its start
            ++out.closedContours;
            continue;
        }
        edges.emplace_back(na, nb);
    }
    parent.resize(nodes.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    for (const auto& [a, b] : edges) {
        ++nodes[static_cast<std::size_t>(a)].degree;
        ++nodes[static_cast<std::size_t>(b)].degree;
        parent[static_cast<std::size_t>(find(a))] = find(b);
    }
    std::map<int, bool> allEven;   // component -> every node has an even degree
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const int c = find(static_cast<int>(i));
        const bool even = nodes[i].degree % 2 == 0;
        if (!even) out.openEnds.push_back(nodes[i].at);
        auto [it, fresh] = allEven.emplace(c, even);
        if (!fresh) it->second = it->second && even;
    }
    for (const auto& [c, even] : allEven) out.closedContours += even ? 1 : 0;

    // Exact repeats: the same kind and the same geometry within the tolerance.
    std::map<std::tuple<int, long long, long long, long long, long long>, std::vector<std::size_t>> buckets;
    const auto round = [&](double v) { return static_cast<long long>(std::llround(v / (tolerance * 10))); };
    for (std::size_t i = 0; i < d.entities.size(); ++i) {
        const Entity& e = d.entities[i];
        if (!wanted(e)) continue;
        const Box2 b = Bounds(e);
        auto& list = buckets[{static_cast<int>(e.kind), round(b.min.x), round(b.min.y), round(b.max.x), round(b.max.y)}];
        const auto same = [&](const Entity& o) {
            if (o.kind != e.kind) return false;
            const auto close = [&](Vec2 p, Vec2 q) { return (p - q).Length() <= tolerance; };
            switch (e.kind) {
                case Kind::Line: return (close(e.a, o.a) && close(e.b, o.b)) || (close(e.a, o.b) && close(e.b, o.a));
                case Kind::Circle: return close(e.center, o.center) && std::abs(e.radius - o.radius) <= tolerance;
                case Kind::Arc: {
                    Vec2 a1, b1, a2, b2;
                    Ends(e, a1, b1), Ends(o, a2, b2);
                    return close(e.center, o.center) && std::abs(e.radius - o.radius) <= tolerance && close(a1, a2) && close(b1, b2);
                }
                case Kind::Polyline: {
                    if (e.points.size() != o.points.size() || e.closed != o.closed) return false;
                    for (std::size_t k = 0; k < e.points.size(); ++k)
                        if (!close(e.points[k], o.points[k])) return false;
                    return true;
                }
                case Kind::Text: return false;
            }
            return false;
        };
        bool dup = false;
        for (const std::size_t j : list)
            if (same(d.entities[j])) {
                dup = true;
                break;
            }
        if (dup) out.duplicates.push_back(i);
        else list.push_back(i);
    }
    return out;
}

int CloseGaps(Drawing& d, std::string_view layer, double maxGap, double tolerance) {
    struct End {
        std::size_t entity;
        bool        first;   ///< the start (else the end)
        Vec2        at;
        bool        fixed;   ///< an arc's end: does not move
    };
    const auto wanted = [&](const Entity& e) { return e.kind != Kind::Text && (layer.empty() || e.layer == layer); };
    std::vector<End> ends;
    for (std::size_t i = 0; i < d.entities.size(); ++i) {
        const Entity& e = d.entities[i];
        Vec2 a, b;
        if (!wanted(e) || !Ends(e, a, b)) continue;
        ends.push_back({i, true, a, e.kind == Kind::Arc});
        ends.push_back({i, false, b, e.kind == Kind::Arc});
    }
    // An end is open when no other end is within the tolerance.
    std::vector<bool> open(ends.size(), true);
    for (std::size_t i = 0; i < ends.size(); ++i)
        for (std::size_t j = 0; j < ends.size(); ++j)
            if (i != j && (ends[i].at - ends[j].at).Length() <= tolerance) open[i] = false;
    // Nearest pairs first.
    std::vector<std::tuple<double, std::size_t, std::size_t>> pairs;
    for (std::size_t i = 0; i < ends.size(); ++i)
        for (std::size_t j = i + 1; j < ends.size(); ++j) {
            if (!open[i] || !open[j]) continue;
            const double gap = (ends[i].at - ends[j].at).Length();
            const bool sameEntity = ends[i].entity == ends[j].entity;
            if (gap <= maxGap && !(sameEntity && d.entities[ends[i].entity].kind != Kind::Polyline)) pairs.emplace_back(gap, i, j);
        }
    std::sort(pairs.begin(), pairs.end());
    const auto put = [&](const End& end, Vec2 p) {
        Entity& e = d.entities[end.entity];
        if (e.kind == Kind::Line) (end.first ? e.a : e.b) = p;
        else if (e.kind == Kind::Polyline) (end.first ? e.points.front() : e.points.back()) = p;
    };
    int closed = 0;
    std::vector<bool> used(ends.size(), false);
    for (const auto& [gap, i, j] : pairs) {
        if (used[i] || used[j]) continue;
        const End &a = ends[i], &b = ends[j];
        if (a.fixed && b.fixed) continue;   // two arcs: cannot meet without changing their shape
        const Vec2 meet = a.fixed ? a.at : b.fixed ? b.at : (a.at + b.at) * 0.5;
        if (a.entity == b.entity) {   // a polyline almost closed: close it
            Entity& e = d.entities[a.entity];
            e.points.back() = e.points.front();
            e.points.pop_back();
            if (e.bulges.size() > e.points.size()) e.bulges.pop_back();
            e.closed = true;
        } else {
            put(a, meet);
            put(b, meet);
        }
        used[i] = used[j] = true;
        ++closed;
    }
    return closed;
}

} // namespace bz::dxf
