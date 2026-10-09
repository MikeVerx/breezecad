#include "sketch/SplineGeometry.h"

#include <algorithm>

namespace bz::sketch {
namespace {

/// Through the points: one Bézier piece per gap, joined C1.
BSpline Interpolating(const std::vector<Vec2>& p, bool closed) {
    const std::size_t n = p.size();   // closed: the last point repeats the first
    const auto tangent = [&](std::size_t i) -> Vec2 {
        if (closed && (i == 0 || i == n - 1)) return (p[1] - p[n - 2]) * 0.5;
        if (i == 0) return p[1] - p[0];
        if (i == n - 1) return p[n - 1] - p[n - 2];
        return (p[i + 1] - p[i - 1]) * 0.5;
    };
    BSpline b;
    b.degree = 3;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (i == 0) b.poles.push_back(p[0]);
        b.poles.push_back(p[i] + tangent(i) * (1.0 / 3));
        b.poles.push_back(p[i + 1] - tangent(i + 1) * (1.0 / 3));
        b.poles.push_back(p[i + 1]);
        b.knots.push_back(static_cast<double>(i));
        b.mults.push_back(i == 0 ? 4 : 3);
    }
    b.knots.push_back(static_cast<double>(n - 1));
    b.mults.push_back(4);
    return b;
}

/// Control vertices: clamped, uniform.
BSpline Controlled(const std::vector<Vec2>& p) {
    BSpline b;
    b.degree = std::min<int>(3, static_cast<int>(p.size()) - 1);
    b.poles = p;
    const int spans = static_cast<int>(p.size()) - b.degree;
    for (int i = 0; i <= spans; ++i) {
        b.knots.push_back(i);
        b.mults.push_back(i == 0 || i == spans ? b.degree + 1 : 1);
    }
    return b;
}

} // namespace

BSpline SplineShape(const std::vector<Vec2>& points, bool interpolate, bool closed) {
    if (points.size() < 2) return {};
    return interpolate ? Interpolating(points, closed) : Controlled(points);
}

BSpline SplineShape(const Sketch& sk, const Spline& spline) {
    std::vector<Vec2> pts;
    for (const ObjectId id : spline.points) pts.push_back(sk.PositionOf(id));
    return SplineShape(pts, spline.interpolate, spline.Closed());
}

std::vector<Vec2> SampleSpline(const BSpline& b, int perSpan) {
    std::vector<Vec2> out;
    if (b.poles.size() < 2 || b.knots.size() < 2) return out;
    // The flat knot vector, for de Boor.
    std::vector<double> u;
    for (std::size_t i = 0; i < b.knots.size(); ++i) u.insert(u.end(), static_cast<std::size_t>(b.mults[i]), b.knots[i]);
    const int p = b.degree;
    const auto eval = [&](double t) {
        // The span: u[k] <= t < u[k+1], within [p, poles).
        std::size_t k = static_cast<std::size_t>(p);
        while (k + 1 < b.poles.size() && u[k + 1] <= t) ++k;
        std::vector<Vec2> d(static_cast<std::size_t>(p) + 1);
        for (int j = 0; j <= p; ++j) d[static_cast<std::size_t>(j)] = b.poles[k - static_cast<std::size_t>(p) + static_cast<std::size_t>(j)];
        for (int r = 1; r <= p; ++r)
            for (int j = p; j >= r; --j) {
                const std::size_t i = k - static_cast<std::size_t>(p) + static_cast<std::size_t>(j);
                const double den = u[i + static_cast<std::size_t>(p) + 1 - static_cast<std::size_t>(r)] - u[i];
                const double a = den > 0 ? (t - u[i]) / den : 0.0;
                d[static_cast<std::size_t>(j)] = d[static_cast<std::size_t>(j) - 1] * (1 - a) + d[static_cast<std::size_t>(j)] * a;
            }
        return d[static_cast<std::size_t>(p)];
    };
    for (std::size_t s = 0; s + 1 < b.knots.size(); ++s)
        for (int i = 0; i < perSpan; ++i) out.push_back(eval(b.knots[s] + (b.knots[s + 1] - b.knots[s]) * i / perSpan));
    out.push_back(b.poles.back());
    return out;
}

} // namespace bz::sketch
