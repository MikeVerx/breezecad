#include "app/sketch/SketchText.h"

namespace bz::app {
namespace {

struct Box {
    Vec2   origin, u, v;   ///< bottom-left corner, unit directions of the bottom and the left side
    double w = 0, h = 0;
};

Box BoxOf(const sketch::Sketch& sk, const sketch::Text& t) {
    Box b;
    b.origin = sk.PositionOf(t.corners[0]);
    const Vec2 along = sk.PositionOf(t.corners[1]) - b.origin, up = sk.PositionOf(t.corners[3]) - b.origin;
    b.w = along.Length();
    b.h = up.Length();
    b.u = b.w > 1e-12 ? along * (1.0 / b.w) : Vec2{1, 0};
    b.v = {-b.u.y, b.u.x};
    return b;
}

} // namespace

kernel::TextFormat FormatOf(const sketch::Text& t) {
    return {t.text, t.height, t.font, t.bold, t.italic, t.hAlign, t.vAlign};
}

std::vector<std::vector<Vec2>> TextLoops(const sketch::Sketch& sk, const sketch::Text& t) {
    const Box b = BoxOf(sk, t);
    std::vector<std::vector<Vec2>> out = kernel::TextOutlines(FormatOf(t), b.w, b.h);
    for (auto& loop : out)
        for (Vec2& p : loop) p = b.origin + b.u * p.x + b.v * p.y;
    return out;
}

bool InsideTextBox(const sketch::Sketch& sk, const sketch::Text& t, Vec2 p) {
    const Box b = BoxOf(sk, t);
    const Vec2 d = p - b.origin;
    const double x = d.Dot(b.u), y = d.Dot(b.v);
    return x >= 0 && x <= b.w && y >= 0 && y <= b.h;
}

} // namespace bz::app
