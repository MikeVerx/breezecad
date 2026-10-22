#include "sketch/Clipboard.h"

#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace bz::sketch {
namespace {

/// Copies `entities` of `from` into `to`, every point shifted by `shift`.
/// Sizes: `rename` gives each its name in `to` (empty: none); `toNumbers`
/// makes their expressions plain values.
std::vector<ObjectId> CopyAcross(const Sketch& from, Sketch& to, IdGenerator& ids, std::span<const ObjectId> entities, Vec2 shift,
                                 const std::function<std::string()>& rename, bool toNumbers) {
    std::unordered_map<ObjectId, ObjectId> map;   // from's points and curves -> to's
    const auto point = [&](ObjectId p) -> ObjectId {
        if (const auto it = map.find(p); it != map.end()) return it->second;
        const ObjectId c = to.AddPoint(ids, from.PositionOf(p) + shift);
        map[p] = c;
        return c;
    };
    std::vector<ObjectId> created;
    const std::unordered_set<ObjectId> chosen(entities.begin(), entities.end());
    // A text's own sides and corners come with the text (it makes them, with
    // its own constraints): never on their own, nor their constraints.
    std::unordered_set<ObjectId> textOwned;
    for (const Text& t : from.texts) {
        textOwned.insert(t.sides.begin(), t.sides.end());
        textOwned.insert(t.corners.begin(), t.corners.end());
    }
    for (const Line& l : from.lines)
        if (chosen.contains(l.id) && !textOwned.contains(l.id)) {
            const ObjectId c = to.AddLine(ids, point(l.start), point(l.end), l.construction);
            to.FindLine(c)->centerline = l.centerline;
            map[l.id] = c;
            created.push_back(c);
        }
    for (const Circle& ci : from.circles)
        if (chosen.contains(ci.id)) {
            const ObjectId c = to.AddCircle(ids, point(ci.center), ci.radius, ci.construction);
            map[ci.id] = c;
            created.push_back(c);
        }
    for (const Arc& a : from.arcs)
        if (chosen.contains(a.id)) {
            const ObjectId c = to.AddArc(ids, point(a.center), point(a.start), point(a.end), a.construction);
            map[a.id] = c;
            created.push_back(c);
        }
    for (const Ellipse& e : from.ellipses)
        if (chosen.contains(e.id)) {
            const ObjectId c = to.AddEllipse(ids, point(e.center), point(e.major), point(e.minor), e.construction);
            map[e.id] = c;
            created.push_back(c);
        }
    for (const Spline& s : from.splines)
        if (chosen.contains(s.id)) {
            std::vector<ObjectId> pts;
            for (const ObjectId p : s.points) pts.push_back(point(p));
            const ObjectId c = to.AddSpline(ids, std::move(pts), s.interpolate, s.construction);
            map[s.id] = c;
            created.push_back(c);
        }
    for (const Text& t : from.texts)
        if (chosen.contains(t.id)) {
            const ObjectId c = to.AddText(ids, from.PositionOf(t.corners[0]) + shift, from.PositionOf(t.corners[2]) + shift, t.text, t.height);
            Text* copy = to.FindText(c);
            for (std::size_t i = 0; i < 4; ++i)
                if (Point* p = to.FindPoint(copy->corners[i])) p->pos = from.PositionOf(t.corners[i]) + shift;
            copy->font = t.font;
            copy->bold = t.bold;
            copy->italic = t.italic;
            copy->hAlign = t.hAlign;
            copy->vAlign = t.vAlign;
            copy->construction = t.construction;
            map[t.id] = c;
            created.push_back(c);
        }
    // Single points chosen on their own (not the origin).
    for (const Point& p : from.points)
        if (chosen.contains(p.id) && !p.isOrigin && !textOwned.contains(p.id)) created.push_back(point(p.id));

    // Constraints wholly among the copied things.
    for (const Constraint& c : from.constraints) {
        if (c.type == ConstraintType::Fix) continue;   // its place belonged to the original
        if (textOwned.contains(c.refs[0]) || textOwned.contains(c.refs[1]) || textOwned.contains(c.refs[2])) continue;
        Constraint copy = c;
        bool inside = true;
        for (ObjectId& r : copy.refs)
            if (r) {
                const auto it = map.find(r);
                if (it == map.end()) {
                    inside = false;
                    break;
                }
                r = it->second;
            }
        if (!inside) continue;
        if (IsDimensional(c.type)) {
            if (toNumbers) {
                char text[64];
                const double v = c.parameter.value;
                if (c.parameter.quantity == Quantity::Length) std::snprintf(text, sizeof text, "%.10gmm", v);
                else if (c.parameter.quantity == Quantity::Angle) std::snprintf(text, sizeof text, "%.10gdeg", v * 180.0 / kPi);
                else std::snprintf(text, sizeof text, "%.10g", v);
                copy.parameter.expression = text;
            }
            copy.parameter.name = rename ? rename() : std::string{};
        }
        to.AddConstraint(ids, copy);
    }
    return created;
}

} // namespace

bool Clipboard::Empty() const {
    return shapes.lines.empty() && shapes.circles.empty() && shapes.arcs.empty() && shapes.ellipses.empty() &&
           shapes.splines.empty() && shapes.texts.empty() && shapes.points.empty();
}

Clipboard CopyToClipboard(const Sketch& sk, std::span<const ObjectId> entities, Vec2 base) {
    Clipboard clip;
    IdGenerator ids;
    CopyAcross(sk, clip.shapes, ids, entities, Vec2{} - base, {}, true);
    // Copied geometry is the user's own now, not model edges.
    for (Point& p : clip.shapes.points) p.projected = false;
    for (Line& l : clip.shapes.lines) l.projected = false;
    for (Circle& c : clip.shapes.circles) c.projected = false;
    for (Arc& a : clip.shapes.arcs) a.projected = false;
    return clip;
}

std::vector<ObjectId> PasteFromClipboard(Sketch& sk, IdGenerator& ids, const Clipboard& clip, Vec2 at,
                                         const std::function<std::string()>& nextName) {
    std::vector<ObjectId> all;
    const Sketch& s = clip.shapes;
    for (const Line& l : s.lines) all.push_back(l.id);
    for (const Circle& c : s.circles) all.push_back(c.id);
    for (const Arc& a : s.arcs) all.push_back(a.id);
    for (const Ellipse& e : s.ellipses) all.push_back(e.id);
    for (const Spline& sp : s.splines) all.push_back(sp.id);
    for (const Text& t : s.texts) all.push_back(t.id);
    // Points that no curve uses (copied on their own); a text's own lines and corners come with it.
    std::unordered_set<ObjectId> used;
    for (const Line& l : s.lines) used.insert({l.start, l.end});
    for (const Circle& c : s.circles) used.insert(c.center);
    for (const Arc& a : s.arcs) used.insert({a.center, a.start, a.end});
    for (const Ellipse& e : s.ellipses) used.insert({e.center, e.major, e.minor});
    for (const Spline& sp : s.splines) used.insert(sp.points.begin(), sp.points.end());
    for (const Text& t : s.texts) used.insert(t.corners.begin(), t.corners.end());
    for (const Point& p : s.points)
        if (!used.contains(p.id)) all.push_back(p.id);
    // A text's sides are lines of the clipboard too: they come with their text, not twice.
    std::unordered_set<ObjectId> sides;
    for (const Text& t : s.texts) sides.insert(t.sides.begin(), t.sides.end());
    std::erase_if(all, [&](ObjectId id) { return sides.contains(id); });
    return CopyAcross(s, sk, ids, all, at, nextName, false);
}

} // namespace bz::sketch
