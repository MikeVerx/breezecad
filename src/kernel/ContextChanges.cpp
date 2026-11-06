// A change of the sketch another part of an assembly is built from: found
// (SourcePrintsOf, FindAffectedSketches) and what the author chooses to do
// with the part (see kernel/ContextProjection.h, ADR-0009).
#include "kernel/ContextProjection.h"

#include "document/Recompute.h"
#include "document/SketchFeature.h"
#include "sketch/Offset.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace bz::kernel {
namespace {

void Mix(std::uint64_t& h, std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); }
void Mix(std::uint64_t& h, double v) {
    // Rounded to a nanometre: noise below that is not a change.
    Mix(h, static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(v * 1e6))));
}
void Mix(std::uint64_t& h, Vec2 v) {
    Mix(h, v.x);
    Mix(h, v.y);
}
void Mix(std::uint64_t& h, Vec3 v) {
    Mix(h, v.x);
    Mix(h, v.y);
    Mix(h, v.z);
}

/// The print of the curve `source` names in `part`, kSourceGone when it is not there.
std::uint64_t PrintOf(const doc::Document& part, const doc::SketchSource& source) {
    const auto* sk = part.FindAs<doc::SketchFeature>(source.sketch);
    if (!sk) return kSourceGone;
    const sketch::Sketch& s = sk->sketch;
    std::uint64_t h = 1469598103934665603ull;
    Mix(h, sk->placement.frame.origin);
    Mix(h, sk->placement.frame.normal);
    Mix(h, sk->placement.frame.xDir);
    if (const sketch::Line* l = s.FindLine(source.curve)) {
        Mix(h, std::uint64_t{1});
        Mix(h, s.PositionOf(l->start));
        Mix(h, s.PositionOf(l->end));
    } else if (const sketch::Circle* c = s.FindCircle(source.curve)) {
        Mix(h, std::uint64_t{2});
        Mix(h, s.PositionOf(c->center));
        Mix(h, c->radius);
    } else if (const sketch::Arc* a = s.FindArc(source.curve)) {
        Mix(h, std::uint64_t{3});
        Mix(h, s.PositionOf(a->center));
        Mix(h, s.PositionOf(a->start));
        Mix(h, s.PositionOf(a->end));
    } else {
        return kSourceGone;
    }
    return h == kSourceGone ? 1 : h;
}

} // namespace

SourcePrints SourcePrintsOf(const AssemblyModel& geometry, const doc::Document& doc, std::string_view assembly) {
    SourcePrints out;
    for (const auto& f : doc.Features()) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(f.get());
        if (!sk) continue;
        for (const doc::ContextProjection& p : sk->contextProjections) {
            if (!p.adaptive || p.assembly != assembly || !p.source.IsSet()) continue;
            // A component taken out of the assembly is not a change of its
            // sketch: its projections go red as before (no print at all).
            if (const doc::Document* part = geometry.PartOf(p.component)) out[p.curve] = PrintOf(*part, p.source);
        }
    }
    return out;
}

std::vector<AffectedSketch> FindAffectedSketches(const doc::Document& doc, std::string_view assembly, const SourcePrints& before,
                                                 const SourcePrints& now) {
    std::vector<AffectedSketch> out;
    for (const auto& f : doc.Features()) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(f.get());
        if (!sk) continue;
        AffectedSketch hit;
        for (const doc::ContextProjection& p : sk->contextProjections) {
            if (!p.adaptive || p.assembly != assembly) continue;
            const auto was = before.find(p.curve);
            if (was == before.end()) continue;   // new since: nothing to compare with
            const auto is = now.find(p.curve);
            if (is == now.end() || is->second == was->second) continue;   // no print now: its part is gone
            if (hit.curves.empty()) {
                hit.sourceComponent = p.component;
                hit.sourceSketch = p.source.sketch;
            }
            hit.curves.push_back(p.curve);
        }
        if (hit.curves.empty()) continue;
        hit.sketch = sk->id;
        hit.name = sk->name;
        out.push_back(std::move(hit));
    }
    return out;
}

int IgnoreSourceChange(doc::Document& doc, std::string_view assembly, const std::vector<AffectedSketch>& affected) {
    int count = 0;
    for (const AffectedSketch& hit : affected) {
        auto* sk = doc.FindAs<doc::SketchFeature>(hit.sketch);
        if (!sk) continue;
        for (doc::ContextProjection& p : sk->contextProjections)
            if (p.adaptive && p.assembly == assembly && std::find(hit.curves.begin(), hit.curves.end(), p.curve) != hit.curves.end()) {
                p.adaptive = false;
                ++count;
            }
    }
    return count;
}

namespace {

using sketch::Sketch;

/// The point ids a curve is drawn through.
std::vector<ObjectId> PointsOf(const Sketch& s, ObjectId curve) {
    if (const sketch::Line* l = s.FindLine(curve)) return {l->start, l->end};
    if (const sketch::Circle* c = s.FindCircle(curve)) return {c->center};
    if (const sketch::Arc* a = s.FindArc(curve)) return {a->center, a->start, a->end};
    return {};
}

/// 1 line, 2 circle, 3 arc, 0 anything else.
int KindOf(const Sketch& s, ObjectId curve) {
    if (s.FindLine(curve)) return 1;
    if (s.FindCircle(curve)) return 2;
    if (s.FindArc(curve)) return 3;
    return 0;
}

/// Where a curve is, roughly: its middle (a circle's or arc's centre is not
/// enough: concentric curves would all be at one place).
Vec2 MiddleOf(const Sketch& s, ObjectId curve) {
    const std::vector<ObjectId> pts = PointsOf(s, curve);
    if (const sketch::Circle* c = s.FindCircle(curve)) return s.PositionOf(c->center) + Vec2{c->radius, 0};
    Vec2 sum{0, 0};
    for (ObjectId p : pts) sum = sum + s.PositionOf(p);
    return pts.empty() ? sum : sum * (1.0 / static_cast<double>(pts.size()));
}

/// Of `candidates`, the curve of `kind` nearest `at`; none when there is no such one.
ObjectId Nearest(const Sketch& s, const std::vector<ObjectId>& candidates, int kind, Vec2 at) {
    ObjectId best;
    double bestDistance = 1e300;
    for (ObjectId c : candidates) {
        if (KindOf(s, c) != kind) continue;
        const double d = (MiddleOf(s, c) - at).Length();
        if (d < bestDistance) bestDistance = d, best = c;
    }
    return best;
}

/// Of the points of `curves`, the one nearest `at`.
ObjectId NearestPoint(const Sketch& s, const std::vector<ObjectId>& curves, Vec2 at) {
    ObjectId best;
    double bestDistance = 1e300;
    for (ObjectId c : curves)
        for (ObjectId p : PointsOf(s, c)) {
            const double d = (s.PositionOf(p) - at).Length();
            if (d < bestDistance) bestDistance = d, best = p;
        }
    return best;
}

/// The chain of `curves` (added in order of travel, neighbours sharing a
/// point), as sketch::Chain: each piece's direction from the shared points.
sketch::Chain ChainThrough(const Sketch& s, const std::vector<ObjectId>& curves, bool closed) {
    sketch::Chain chain;
    chain.closed = closed;
    const auto ends = [&](ObjectId c) -> std::pair<ObjectId, ObjectId> {
        if (const sketch::Line* l = s.FindLine(c)) return {l->start, l->end};
        if (const sketch::Arc* a = s.FindArc(c)) return {a->start, a->end};
        return {};
    };
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const auto [a, b] = ends(curves[i]);
        bool reversed = false;
        if (i + 1 < curves.size() || (closed && curves.size() > 1)) {
            const auto [na, nb] = ends(curves[(i + 1) % curves.size()]);
            reversed = a == na || a == nb;   // its start is where the next one goes on
        } else if (i > 0) {
            const auto [pa, pb] = ends(curves[i - 1]);
            reversed = b == pa || b == pb;
        }
        chain.pieces.push_back({curves[i], reversed});
    }
    return chain;
}

/// Twice the signed area a closed chain of lines encloses (corners only): > 0 counter-clockwise.
double SignedArea(const Sketch& s, const sketch::Chain& chain) {
    double area = 0;
    for (const sketch::Chain::Piece& piece : chain.pieces) {
        const std::vector<ObjectId> pts = PointsOf(s, piece.curve);
        if (s.FindArc(piece.curve)) {   // an arc: its ends
            const Vec2 p = s.PositionOf(piece.reversed ? pts[2] : pts[1]), q = s.PositionOf(piece.reversed ? pts[1] : pts[2]);
            area += p.x * q.y - q.x * p.y;
        } else if (pts.size() == 2) {
            const Vec2 p = s.PositionOf(piece.reversed ? pts[1] : pts[0]), q = s.PositionOf(piece.reversed ? pts[0] : pts[1]);
            area += p.x * q.y - q.x * p.y;
        }
    }
    return area;
}

} // namespace

Expected<int> MimicSourceChange(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId target, doc::Document& doc,
                                std::string_view assembly, const std::vector<AffectedSketch>& affected, double gap) {
    if (!(gap >= 0.0)) return Error{"Зазор должен быть не меньше нуля"};
    const doc::Document backup = doc.Clone();
    int made = 0;
    std::size_t first = static_cast<std::size_t>(-1);
    for (const AffectedSketch& hit : affected) {
        auto* sk = doc.FindAs<doc::SketchFeature>(hit.sketch);
        const doc::Document* sourcePart = geometry.PartOf(hit.sourceComponent);
        const auto* sourceSketch = sourcePart ? sourcePart->FindAs<doc::SketchFeature>(hit.sourceSketch) : nullptr;
        if (!sk || !sourceSketch) {
            doc = backup.Clone();
            return Error{"Эскиз-источник удалён: повторять нечего"};
        }
        Sketch& s = sk->sketch;
        IdGenerator& ids = doc.Ids();

        // The old contour: projections of that source sketch.
        std::vector<ObjectId> oldProjected;
        doc::SketchSource seed;
        for (const doc::ContextProjection& p : sk->contextProjections) {
            if (p.assembly != assembly || p.component != hit.sourceComponent || p.source.sketch != hit.sourceSketch) continue;
            oldProjected.push_back(p.curve);
            if (!seed.IsSet() && KindOf(sourceSketch->sketch, p.source.curve) != 0) seed = p.source;
        }
        if (!seed.IsSet()) {
            doc = backup.Clone();
            return Error{"Контур-источник удалён: повторять нечего"};
        }
        const std::optional<sketch::Chain> contour = sketch::ChainOf(sourceSketch->sketch, seed.curve);
        if (!contour) {
            doc = backup.Clone();
            return Error{"Контур-источник не найден"};
        }

        // The new contour, projected: one curve per piece, neighbours sharing their points.
        std::vector<ObjectId> newProjected;
        std::vector<doc::ContextProjection> links;
        std::vector<ObjectId> newPoints;
        const auto pointAt = [&](Vec2 at) {
            for (ObjectId p : newPoints)
                if ((s.PositionOf(p) - at).Length() < 1e-6) return p;
            const ObjectId p = s.AddPoint(ids, at);
            s.FindPoint(p)->projected = true;
            newPoints.push_back(p);
            return p;
        };
        for (const sketch::Chain::Piece& piece : contour->pieces) {
            doc::ContextProjection link{std::string(assembly), hit.sourceComponent, {}, {}, true,
                                        doc::SketchSource{hit.sourceSketch, piece.curve, seed.offset, 0.0, 1.0}};
            doc::EdgeReference found;
            doc::SketchSource source = link.source;
            const Expected<ProjectedCurve> pc = geometry.ProjectSourceEdge(a, target, link, sk->placement.frame, &found, &source);
            if (!pc || pc->kind == ProjectedCurve::Kind::Ellipse) {
                doc = backup.Clone();
                return Error{"Новый контур не проецируется на плоскость эскиза"};
            }
            ObjectId curve;
            if (pc->kind == ProjectedCurve::Kind::Line) {
                curve = s.AddLine(ids, pointAt(pc->start), pointAt(pc->end));
                s.FindLine(curve)->projected = true;
            } else if (pc->kind == ProjectedCurve::Kind::Circle) {
                curve = s.AddCircle(ids, pointAt(pc->centre), pc->radius);
                s.FindCircle(curve)->projected = true;
            } else {
                curve = s.AddArc(ids, pointAt(pc->centre), pointAt(pc->start), pointAt(pc->end));
                for (sketch::Arc& arc : s.arcs)
                    if (arc.id == curve) arc.projected = true;
            }
            link.edge = found;
            link.source = source;
            link.curve = curve;
            links.push_back(link);
            newProjected.push_back(curve);
        }
        const sketch::Chain newChain = ChainThrough(s, newProjected, contour->closed);

        // The old outline: curves (not projected) tied to the old projections, with their chains.
        std::vector<ObjectId> oldPoints;
        for (ObjectId c : oldProjected)
            for (ObjectId p : PointsOf(s, c)) oldPoints.push_back(p);
        const auto isOld = [&](ObjectId id) {
            return std::find(oldProjected.begin(), oldProjected.end(), id) != oldProjected.end() ||
                   std::find(oldPoints.begin(), oldPoints.end(), id) != oldPoints.end();
        };
        std::vector<ObjectId> oldOutline;
        const auto curveOfPoint = [&](ObjectId point) -> std::vector<ObjectId> {
            std::vector<ObjectId> out;
            for (const sketch::Line& l : s.lines)
                if (!l.projected && !l.construction && (l.id == point || l.start == point || l.end == point)) out.push_back(l.id);
            for (const sketch::Arc& arc : s.arcs)
                if (!arc.projected && !arc.construction && (arc.id == point || arc.center == point))
                    out.push_back(arc.id);
            for (const sketch::Circle& c : s.circles)
                if (!c.projected && !c.construction && (c.id == point || c.center == point)) out.push_back(c.id);
            return out;
        };
        // Only what an offset ties: parallel lines, a distance from the
        // contour, a shared or concentric centre — a hole dimensioned from
        // the contour is not its outline.
        for (ObjectId p : oldPoints)   // arcs of an offset share their centre with the contour's
            for (ObjectId curve : curveOfPoint(p))
                if (s.FindArc(curve) && std::find(oldOutline.begin(), oldOutline.end(), curve) == oldOutline.end())
                    oldOutline.push_back(curve);
        for (const sketch::Constraint& c : s.constraints) {
            using T = sketch::ConstraintType;
            if (c.type != T::Parallel && c.type != T::PointLineDistance && c.type != T::Concentric) continue;
            if (!(isOld(c.refs[0]) || isOld(c.refs[1]) || isOld(c.refs[2]))) continue;
            for (ObjectId curve : c.refs)
                if (curve && !isOld(curve) && !s.FindPoint(curve) && !curveOfPoint(curve).empty())
                    if (const auto chain = sketch::ChainOf(s, curve))
                        for (const auto& piece : chain->pieces)
                            if (std::find(oldOutline.begin(), oldOutline.end(), piece.curve) == oldOutline.end())
                                oldOutline.push_back(piece.curve);
        }

        // The side of the offset: where the old outline was; outside when there was none.
        int side = 0;
        if (!oldOutline.empty()) side = sketch::SideOf(s, newChain, MiddleOf(s, oldOutline.front()));
        else if (newChain.closed) side = SignedArea(s, newChain) > 0 ? -1 : 1;
        if (side == 0) side = 1;
        const std::optional<sketch::OffsetCurves> outline = sketch::OffsetChain(s, ids, newChain, side * gap);
        if (!outline) {
            doc = backup.Clone();
            return Error{"Эквидистанта нового контура не строится на этом зазоре"};
        }
        made += static_cast<int>(outline->curves.size());
        std::string driver;
        for (const auto& [point, line] : outline->distances) {
            sketch::Constraint c;
            c.type = sketch::ConstraintType::PointLineDistance;
            c.refs = {point, line, {}};
            c.parameter.name = doc.NextParameterName();
            c.parameter.expression = driver.empty() ? std::format("{}", gap) : driver;
            if (driver.empty()) driver = c.parameter.name;
            s.AddConstraint(ids, c);
            doc.EvaluateParameters();
        }

        // The old curves' own constraints go over to the new ones; those
        // between the old contour and its outline (the old offset) go away.
        std::vector<ObjectId> outlinePoints;
        for (ObjectId c : oldOutline)
            for (ObjectId p : PointsOf(s, c)) outlinePoints.push_back(p);
        const auto inOutline = [&](ObjectId id) {
            return std::find(oldOutline.begin(), oldOutline.end(), id) != oldOutline.end() ||
                   std::find(outlinePoints.begin(), outlinePoints.end(), id) != outlinePoints.end();
        };
        const auto remap = [&](ObjectId id) -> ObjectId {
            const bool fromContour = isOld(id);
            if (!fromContour && !inOutline(id)) return id;
            const std::vector<ObjectId>& into = fromContour ? newProjected : outline->curves;
            if (s.FindPoint(id)) return NearestPoint(s, into, s.PositionOf(id));
            return Nearest(s, into, KindOf(s, id), MiddleOf(s, id));
        };
        std::vector<ObjectId> drop;
        for (sketch::Constraint& c : s.constraints) {
            const bool contour = isOld(c.refs[0]) || isOld(c.refs[1]) || isOld(c.refs[2]);
            const bool around = inOutline(c.refs[0]) || inOutline(c.refs[1]) || inOutline(c.refs[2]);
            if (!contour && !around) continue;
            // The old offset itself, and the outline's own dimensions (made
            // for the old contour): gone. The rest goes over to the new curves.
            const bool inside = std::all_of(c.refs.begin(), c.refs.end(), [&](ObjectId r) { return !r || inOutline(r); });
            if ((contour && around) || (around && inside && sketch::IsDimensional(c.type))) {
                drop.push_back(c.id);
                continue;
            }
            for (ObjectId& r : c.refs) {
                if (!r) continue;
                const ObjectId to = remap(r);
                if (!to) {
                    drop.push_back(c.id);
                    break;
                }
                r = to;
            }
            if (c.refs[0] && c.refs[0] == c.refs[1]) drop.push_back(c.id);   // two old points became one
        }
        for (ObjectId c : drop) s.Remove(c);
        for (ObjectId c : oldOutline) s.Remove(c);
        for (ObjectId c : oldProjected) s.Remove(c);
        sk->PruneProjections();
        for (doc::ContextProjection& link : links) sk->contextProjections.push_back(std::move(link));
        sk->lostContextProjections.clear();
        sk->beforeBreak.reset();
        doc::SolveSketch(*sk);
        first = std::min(first, static_cast<std::size_t>(doc.IndexOf(sk->id)));
    }
    if (first != static_cast<std::size_t>(-1)) doc.MarkChanged(first);
    return made;
}

} // namespace bz::kernel
