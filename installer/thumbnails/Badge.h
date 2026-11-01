#pragma once
// The BreezeCAD mark drawn into the corner of a preview (thumbnail provider)
// without any graphics library: tested point by point with 4x4 samples per
// pixel. Plain C++, so it is checked on any system (tests/installer).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bz::thumbs {

// --- the mark -----------------------------------------------------------------
// assets/brand/breezecad-mark.svg, in its own units (viewBox 14 6 91 108),
// tested point by point: the badge is small, so it is drawn with 4x4
// samples per pixel instead of carrying an SVG renderer.

struct P {
    double x, y;
};

inline double SegmentDistance(P p, P a, P b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = dx * dx + dy * dy;
    double t = len > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len : 0;
    t = std::clamp(t, 0.0, 1.0);
    const double ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

inline const std::vector<P>& CutLine() {
    static const std::vector<P> line = [] {
        // M38 63 C49 54 59 53 69 57.5 S86 60 100 54 (the S reflects 59,53 about 69,57.5)
        const std::array<std::array<P, 4>, 2> curves = {{{{{38, 63}, {49, 54}, {59, 53}, {69, 57.5}}},
                                                         {{{69, 57.5}, {79, 62}, {86, 60}, {100, 54}}}}};
        std::vector<P> out;
        for (const auto& c : curves)
            for (int i = 0; i <= 24; ++i) {
                const double t = i / 24.0, u = 1 - t;
                const double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
                out.push_back({a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x, a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y});
            }
        return out;
    }();
    return line;
}

inline bool InRoundedRect(P p, double x0, double y0, double x1, double y1, double r) {
    if (p.x < x0 || p.x > x1 || p.y < y0 || p.y > y1) return false;
    const double cx = std::clamp(p.x, x0 + r, x1 - r), cy = std::clamp(p.y, y0 + r, y1 - r);
    return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r;
}

inline bool InD(P p, double x0, double y0, double x1, double y1) {
    // A rectangle with a half disc on its right side (the bowls of the B).
    const double r = (y1 - y0) / 2, cy = (y0 + y1) / 2;
    if (p.y < y0 || p.y > y1 || p.x < x0) return false;
    if (p.x <= x1) return true;
    return (p.x - x1) * (p.x - x1) + (p.y - cy) * (p.y - cy) <= r * r;
}

inline bool InMark(P p) {
    if (InRoundedRect(p, 22, 14, 40, 106, 3)) return true;
    if (!InD(p, 47, 14, 69, 58) && !InD(p, 47, 58, 73, 106)) return false;
    const auto& line = CutLine();
    for (std::size_t i = 1; i < line.size(); ++i)
        if (SegmentDistance(p, line[i - 1], line[i]) <= 3.5) return false;
    return true;
}

struct Rgba {
    double r, g, b, a;   // straight alpha, 0..1
};

inline Rgba Over(Rgba top, Rgba under) {
    const double a = top.a + under.a * (1 - top.a);
    if (a <= 0) return {0, 0, 0, 0};
    return {(top.r * top.a + under.r * under.a * (1 - top.a)) / a, (top.g * top.a + under.g * under.a * (1 - top.a)) / a,
            (top.b * top.a + under.b * under.a * (1 - top.a)) / a, a};
}

/// The badge in the bottom right corner of a size x size BGRA picture
/// (straight alpha, rows top to bottom): a white rounded square with a thin
/// border and the blue mark, like the program's own icon.
inline void DrawBadge(std::vector<std::uint8_t>& bgra, int size) {
    const double side = std::round(size * 0.27), margin = std::round(size * 0.035);
    const double x0 = size - margin - side, y0 = size - margin - side;
    const double radius = side * 0.22, border = std::max(1.0, side * 0.03);
    // The mark fills the badge with 18 % padding, centred on its viewBox.
    const double pad = side * 0.18, scale = (side - 2 * pad) / 108.0;
    const double mx = x0 + (side - 91 * scale) / 2, my = y0 + pad;
    const Rgba white{1, 1, 1, 1}, line{0.78, 0.81, 0.85, 1}, blue{0x1D / 255.0, 0x4E / 255.0, 0xD8 / 255.0, 1};
    const Rgba shadow{0, 0, 0, 0.16};

    const int px0 = std::max(0, int(x0) - 2), py0 = std::max(0, int(y0) - 2);
    for (int y = py0; y < size; ++y)
        for (int x = px0; x < size; ++x) {
            Rgba badge{0, 0, 0, 0};
            constexpr int n = 4;
            double sr = 0, sg = 0, sb = 0, sa = 0;
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    const P p{x + (i + 0.5) / n, y + (j + 0.5) / n};
                    Rgba s{0, 0, 0, 0};
                    if (InRoundedRect({p.x - side * 0.02, p.y - side * 0.03}, x0, y0, x0 + side, y0 + side, radius)) s = shadow;
                    if (InRoundedRect(p, x0, y0, x0 + side, y0 + side, radius)) {
                        s = line;
                        if (InRoundedRect(p, x0 + border, y0 + border, x0 + side - border, y0 + side - border, radius - border)) s = white;
                        if (InMark({(p.x - mx) / scale + 14, (p.y - my) / scale + 6})) s = blue;
                    }
                    sr += s.r * s.a, sg += s.g * s.a, sb += s.b * s.a, sa += s.a;
                }
            if (sa <= 0) continue;
            badge = {sr / sa, sg / sa, sb / sa, sa / (n * n)};
            std::uint8_t* px = &bgra[(std::size_t(y) * size + x) * 4];
            const Rgba under{px[2] / 255.0, px[1] / 255.0, px[0] / 255.0, px[3] / 255.0};
            const Rgba out = Over(badge, under);
            px[0] = std::uint8_t(std::lround(out.b * 255));
            px[1] = std::uint8_t(std::lround(out.g * 255));
            px[2] = std::uint8_t(std::lround(out.r * 255));
            px[3] = std::uint8_t(std::lround(out.a * 255));
        }
}

} // namespace bz::thumbs
