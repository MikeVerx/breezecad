#pragma once
// Puts projected curves and points into a sketch («Проецировать геометрию»):
// the part of the tool that does not depend on the view, so tests reach it.

#include "document/Document.h"
#include "document/SketchFeature.h"
#include "app/Format.h"
#include "document/Origin.h"
#include "kernel/ProjectionSources.h"

#include <algorithm>
#include <cmath>

#include <vector>

namespace bz::app {

using Of = doc::Projection::Of;

/// Puts projected curves and points into a sketch: shares the ends of
/// neighbouring curves, skips what is already there, links what follows the
/// model (a doc::Projection), or makes an independent copy held by Fix
/// constraints (and a diameter for circles).
class Projector {
public:
    doc::Document&      doc;
    doc::SketchFeature& f;
    sketch::Sketch&     sk;
    IdGenerator&        ids;
    double              tolerance;
    bool                linked;
    bool                construction;
    std::vector<ObjectId> placed;   ///< points this click put (an independent projection unmarks them at once)
    int                 added = 0, already = 0;
    ObjectId            last;       ///< the curve or point the last call put

    Projector(doc::Document& d, doc::SketchFeature& feature, double tol, bool link, bool constr)
        : doc(d), f(feature), sk(feature.sketch), ids(d.Ids()), tolerance(tol), linked(link), construction(constr) {}

    /// The point at `at`: a projected one already there (a shared corner), else a new projected point.
    ObjectId Point(Vec2 at) {
        for (const sketch::Point& p : sk.points)
            if ((p.projected || std::ranges::find(placed, p.id) != placed.end()) && (p.pos - at).Length() < tolerance) return p.id;
        const ObjectId id = sk.AddPoint(ids, at);
        sk.FindPoint(id)->projected = true;
        placed.push_back(id);
        return id;
    }

    /// The same curve already in the sketch (a body of revolution clicked a
    /// second time): drawn twice it doubled every region edge.
    bool Present(const kernel::ProjectedCurve& c) const {
        const auto close = [&](Vec2 a, Vec2 b) { return (a - b).Length() < tolerance; };
        switch (c.kind) {
            case kernel::ProjectedCurve::Kind::Line:
                return std::ranges::any_of(sk.lines, [&](const sketch::Line& l) {
                    const Vec2 a = sk.PositionOf(l.start), b = sk.PositionOf(l.end);
                    return (close(a, c.start) && close(b, c.end)) || (close(a, c.end) && close(b, c.start));
                });
            case kernel::ProjectedCurve::Kind::Circle:
                return std::ranges::any_of(sk.circles, [&](const sketch::Circle& o) {
                    return close(sk.PositionOf(o.center), c.centre) && std::abs(o.radius - c.radius) < tolerance;
                });
            case kernel::ProjectedCurve::Kind::Arc:
                return std::ranges::any_of(sk.arcs, [&](const sketch::Arc& a) {
                    return close(sk.PositionOf(a.center), c.centre) && close(sk.PositionOf(a.start), c.start) &&
                           close(sk.PositionOf(a.end), c.end);
                });
            case kernel::ProjectedCurve::Kind::Ellipse:
                return std::ranges::any_of(sk.ellipses, [&](const sketch::Ellipse& e) {
                    return close(sk.PositionOf(e.center), c.centre) && close(sk.PositionOf(e.major), c.start) &&
                           close(sk.PositionOf(e.minor), c.end);
                });
        }
        return false;
    }

    /// The same source projected before (a vertex, a work element, a curve of
    /// another sketch, a piece of a flat pattern).
    bool Known(const doc::Projection& link) const {
        return std::ranges::any_of(f.projections, [&](const doc::Projection& p) {
            if (p.of != link.of) return false;
            switch (link.of) {
                case Of::Edge: return p.edge.curve == link.edge.curve && (p.edge.middle - link.edge.middle).Length() < 1e-6;
                case Of::Vertex: return (p.at - link.at).Length() < 1e-6;
                case Of::Work: return p.work == link.work;
                case Of::Sketch: return p.source.sketch == link.source.sketch && p.source.curve == link.source.curve;
                case Of::Flat: return p.work == link.work && p.part == link.part;
            }
            return false;
        });
    }

    void Remember(const doc::Projection* link) {
        if (!linked || !link) return;
        doc::Projection p = *link;
        p.curve = last;
        f.projections.push_back(p);
    }

    /// An independent copy: the point is ordinary geometry held where it is.
    void Hold(ObjectId point) {
        sketch::Point* pt = sk.FindPoint(point);
        if (!pt || !pt->projected) return;
        pt->projected = false;
        sketch::Constraint fix;
        fix.type = sketch::ConstraintType::Fix;
        fix.refs = {point, {}, {}};
        fix.fixedAt = pt->pos;
        sk.AddConstraint(ids, fix);
    }

    /// Puts one projected curve into the sketch; `link` makes it follow its
    /// source. False when there is nothing to draw or it is there already.
    bool Curve(const kernel::ProjectedCurve& projected, const doc::Projection* link, bool axis = false) {
        if (link && link->of != Of::Edge && Known(*link)) {
            ++already;
            return false;
        }
        if (!link && Present(projected)) {
            ++already;
            return false;
        }
        ObjectId curve;
        switch (projected.kind) {
            case kernel::ProjectedCurve::Kind::Line:
                // An edge seen end-on projects to a point: nothing to draw.
                if ((projected.end - projected.start).Length() < tolerance) return false;
                curve = sk.AddLine(ids, Point(projected.start), Point(projected.end));
                sk.FindLine(curve)->projected = true;
                sk.FindLine(curve)->centerline = axis;
                break;
            case kernel::ProjectedCurve::Kind::Circle:
                curve = sk.AddCircle(ids, Point(projected.centre), projected.radius);
                sk.FindCircle(curve)->projected = true;
                break;
            case kernel::ProjectedCurve::Kind::Arc:
                curve = sk.AddArc(ids, Point(projected.centre), Point(projected.start), Point(projected.end));
                for (auto& a : sk.arcs)
                    if (a.id == curve) a.projected = true;
                break;
            case kernel::ProjectedCurve::Kind::Ellipse:
                // Held by its three points (projected, so fixed for the solver).
                curve = sk.AddEllipse(ids, Point(projected.centre), Point(projected.start), Point(projected.end));
                break;
        }
        last = curve;
        ++added;
        if (construction && !axis) {   // "Вспомогат." chosen before picking
            if (sketch::Line* l = sk.FindLine(curve)) l->construction = true;
            if (sketch::Circle* c = sk.FindCircle(curve)) c->construction = true;
            for (auto& a : sk.arcs)
                if (a.id == curve) a.construction = true;
            for (auto& e : sk.ellipses)
                if (e.id == curve) e.construction = true;
        }
        if (linked) {
            Remember(link);
            return true;
        }
        // Независимая: ordinary geometry held where it is.
        std::vector<ObjectId> own;
        if (sketch::Line* l = sk.FindLine(curve)) {
            l->projected = false;
            own = {l->start, l->end};
        }
        if (sketch::Circle* c = sk.FindCircle(curve)) {
            c->projected = false;
            own = {c->center};
            sketch::Constraint size;   // the size held too, as a dimension
            size.type = sketch::ConstraintType::Diameter;
            size.refs = {curve, {}, {}};
            size.parameter.name = doc.NextParameterName();
            size.parameter.expression = FormatValue(2 * c->radius, Quantity::Length, doc.units, 4);
            size.parameter.quantity = Quantity::Length;
            size.parameter.value = 2 * c->radius;
            sk.AddConstraint(ids, size);
        }
        if (const sketch::Ellipse* e = sk.FindEllipse(curve)) own = {e->center, e->major, e->minor};
        for (auto& a : sk.arcs)
            if (a.id == curve) {
                a.projected = false;
                own = {a.center, a.start, a.end};
            }
        for (const ObjectId p : own) Hold(p);
        return true;
    }

    /// A lone projected point (a vertex, a work point, the origin, an axis
    /// seen end-on). A point already there (the sketch's own origin too) is not doubled.
    bool LonePoint(Vec2 at, const doc::Projection* link) {
        if (link && Known(*link)) {
            ++already;
            return false;
        }
        for (const sketch::Point& p : sk.points)
            if ((p.isOrigin || p.projected) && (p.pos - at).Length() < tolerance) {
                ++already;
                return false;
            }
        const ObjectId id = sk.AddPoint(ids, at);
        sk.FindPoint(id)->projected = true;
        last = id;
        ++added;
        if (linked) Remember(link);
        else Hold(id);
        return true;
    }

    /// A curve or a point.
    bool Item(const kernel::ProjectedItem& item, const doc::Projection* link) {
        if (item.point) return LonePoint(*item.point, link);
        return item.curve && Curve(*item.curve, link);
    }
};


/// «Проецировать начало координат при создании эскиза»: a linked point where
/// the part's origin falls on `sketch`, unless the sketch's own origin is there.
/// False when nothing was added.
inline bool ProjectPartOrigin(doc::Document& doc, doc::SketchFeature& sketch) {
    const Vec2 at = sketch.placement.frame.ToLocal({0, 0, 0});
    if (at.Length() < 1e-9) return false;   // the sketch's own origin is the part's
    Projector put(doc, sketch, 1e-6, true, false);
    doc::Projection link;
    link.of = Of::Work;
    link.work = doc::origin::kPoint;
    return put.LonePoint(at, &link);
}

} // namespace bz::app
