#include "document/SketchFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

#include <array>
#include <format>
#include <utility>

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;
using json_io::ToJson;
using sketch::ConstraintType;

constexpr std::array<std::pair<ConstraintType, std::string_view>, 22> kConstraintIds{{
    {ConstraintType::Coincident, "coincident"},
    {ConstraintType::Horizontal, "horizontal"},
    {ConstraintType::Vertical, "vertical"},
    {ConstraintType::Parallel, "parallel"},
    {ConstraintType::Perpendicular, "perpendicular"},
    {ConstraintType::Equal, "equal"},
    {ConstraintType::Fix, "fix"},
    {ConstraintType::PointOnCurve, "pointOnCurve"},
    {ConstraintType::Midpoint, "midpoint"},
    {ConstraintType::Tangent, "tangent"},
    {ConstraintType::Concentric, "concentric"},
    {ConstraintType::Symmetric, "symmetric"},
    {ConstraintType::Collinear, "collinear"},
    {ConstraintType::Distance, "distance"},
    {ConstraintType::DistanceX, "distanceX"},
    {ConstraintType::DistanceY, "distanceY"},
    {ConstraintType::Length, "length"},
    {ConstraintType::PointLineDistance, "pointLineDistance"},
    {ConstraintType::Radius, "radius"},
    {ConstraintType::Diameter, "diameter"},
    {ConstraintType::Angle, "angle"},
    {ConstraintType::ArcAngle, "arcAngle"},
}};

Error Bad(std::string_view what) { return Error{std::format("Эскиз: повреждено поле «{}»", what)}; }

Json SourceToJson(const SketchSource& s) {
    Json j{{"sketch", ToJson(s.sketch)}, {"curve", ToJson(s.curve)}, {"offset", s.offset}};
    if (s.from != 0.0 || s.to != 1.0) j["range"] = Json::array({s.from, s.to});
    return j;
}

/// The member "source" of a projection; absent (older files): unset.
bool ReadSource(const Json& projection, SketchSource& out) {
    const auto it = projection.find("source");
    if (it == projection.end()) return true;
    if (!it->is_object() || !ReadMember(*it, "sketch", out.sketch) || !ReadMember(*it, "curve", out.curve) ||
        !ReadMember(*it, "offset", out.offset))
        return false;
    if (const auto r = it->find("range"); r != it->end()) {
        if (!r->is_array() || r->size() != 2 || !(*r)[0].is_number() || !(*r)[1].is_number()) return false;
        out.from = (*r)[0].get<double>();
        out.to = (*r)[1].get<double>();
    }
    return true;
}

} // namespace

std::string_view ConstraintTypeId(ConstraintType type) {
    for (const auto& [t, id] : kConstraintIds)
        if (t == type) return id;
    return "coincident";
}

bool ConstraintTypeFromId(std::string_view id, ConstraintType& out) {
    for (const auto& [t, name] : kConstraintIds)
        if (name == id) {
            out = t;
            return true;
        }
    return false;
}

std::vector<ObjectId> SketchFeature::Dependencies() const {
    // A face's body or a work plane; origin planes are not features.
    if (placement.support && !origin::IsOriginElement(placement.support)) return {placement.support};
    return {};
}

void SketchFeature::CollectParameters(std::vector<Parameter*>& out) {
    for (auto& c : sketch.constraints)
        if (sketch::IsDimensional(c.type)) out.push_back(&c.parameter);
}

void SketchFeature::ReserveIds(IdGenerator& ids) const {
    ids.Reserve(id);
    for (const auto& p : sketch.points) ids.Reserve(p.id);
    for (const auto& l : sketch.lines) ids.Reserve(l.id);
    for (const auto& c : sketch.circles) ids.Reserve(c.id);
    for (const auto& a : sketch.arcs) ids.Reserve(a.id);
    for (const auto& s : sketch.splines) ids.Reserve(s.id);
    for (const auto& t : sketch.texts) ids.Reserve(t.id);
    for (const auto& e : sketch.ellipses) ids.Reserve(e.id);
    for (const auto& c : sketch.constraints) ids.Reserve(c.id);
}

void SketchFeature::PruneProjections() {
    std::erase_if(projections, [&](const Projection& p) { return sketch.KindOf(p.curve) == sketch::EntityKind::None; });
    std::erase_if(contextProjections,
                  [&](const ContextProjection& p) { return sketch.KindOf(p.curve) == sketch::EntityKind::None; });
}

std::unordered_set<ObjectId> SketchFeature::BrokenProjections() const {
    std::unordered_set<ObjectId> out = lostProjections;
    out.insert(lostContextProjections.begin(), lostContextProjections.end());
    if (analysis.failing.empty() || (projections.empty() && contextProjections.empty())) return out;
    // A constraint that fails on a projected curve or on one of its points:
    // the curve came where its edge projects now and the sketch cannot follow.
    const auto failsOn = [&](ObjectId entity) {
        for (ObjectId f : analysis.failing)
            if (const sketch::Constraint* c = sketch.FindConstraint(f); c && c->References(entity)) return true;
        return false;
    };
    const auto check = [&](ObjectId curve) {
        if (out.count(curve)) return;
        bool fails = failsOn(curve);
        if (const sketch::Line* l = sketch.FindLine(curve)) fails = fails || failsOn(l->start) || failsOn(l->end);
        else if (const sketch::Circle* c = sketch.FindCircle(curve)) fails = fails || failsOn(c->center);
        else if (const sketch::Arc* a = sketch.FindArc(curve)) fails = fails || failsOn(a->center) || failsOn(a->start) || failsOn(a->end);
        if (fails) out.insert(curve);
    };
    for (const Projection& p : projections) check(p.curve);
    for (const ContextProjection& p : contextProjections) check(p.curve);
    return out;
}

void SketchFeature::NoteProjectionsUpdated(const sketch::Sketch& before, bool wasBroken) {
    if (!HasBrokenProjections()) beforeBreak.reset();          // all well again: nothing to come back to
    else if (!wasBroken && !beforeBreak) beforeBreak = before;   // it broke just now: the sketch from before
}

bool SketchFeature::RestoreBeforeBreak(IdGenerator& ids) {
    if (projections.empty() && contextProjections.empty()) return false;
    if (beforeBreak) sketch = std::move(*beforeBreak);
    beforeBreak.reset();
    // Unlinked: ordinary geometry, held where it is (the points of what was
    // projected are fixed; a circle keeps its radius until dimensioned).
    std::vector<ObjectId> held;
    for (sketch::Point& p : sketch.points)
        if (p.projected) {
            p.projected = false;
            held.push_back(p.id);
        }
    for (sketch::Line& l : sketch.lines) l.projected = false;
    for (sketch::Circle& c : sketch.circles) c.projected = false;
    for (sketch::Arc& a : sketch.arcs) a.projected = false;
    for (ObjectId p : held) {
        sketch::Constraint fix;
        fix.type = sketch::ConstraintType::Fix;
        fix.refs = {p, {}, {}};
        fix.fixedAt = sketch.PositionOf(p);
        sketch.AddConstraint(ids, fix);
    }
    projections.clear();
    contextProjections.clear();
    lostProjections.clear();
    lostContextProjections.clear();
    analysis = {};
    return true;
}

void SketchFeature::SaveFields(Json& out) const {
    out["placement"] = Json{{"support", ToJson(placement.support)},
                            {"frame", ToJson(placement.frame)},
                            {"onFace", placement.onFace}};
    if (placement.face.IsSet()) out["placement"]["face"] = json_io::ToJson(placement.face);
    if (!projections.empty()) {
        Json list = Json::array();
        for (const Projection& p : projections) {
            Json j{{"curve", ToJson(p.curve)}};
            if (p.of == Projection::Of::Edge || p.of == Projection::Of::Vertex) j["edge"] = json_io::ToJson(p.edge);
            if (p.source.IsSet()) j["source"] = SourceToJson(p.source);
            switch (p.of) {   // a model edge (the only kind before 0.2.6) writes no "of"
                case Projection::Of::Edge: break;
                case Projection::Of::Vertex:
                    j["of"] = "vertex";
                    j["at"] = ToJson(p.at);
                    break;
                case Projection::Of::Work:
                    j["of"] = "work";
                    j["work"] = ToJson(p.work);
                    break;
                case Projection::Of::Sketch: j["of"] = "sketch"; break;
                case Projection::Of::Flat:
                    j["of"] = "flat";
                    j["work"] = ToJson(p.work);
                    j["part"] = p.part;
                    break;
            }
            list.push_back(std::move(j));
        }
        out["projections"] = std::move(list);
    }
    if (!contextProjections.empty()) {
        Json list = Json::array();
        for (const ContextProjection& p : contextProjections) {
            Json j{{"assembly", p.assembly}, {"component", ToJson(p.component)}, {"edge", json_io::ToJson(p.edge)},
                   {"curve", ToJson(p.curve)}};
            if (!p.adaptive) j["adaptive"] = false;
            if (p.source.IsSet()) j["source"] = SourceToJson(p.source);
            list.push_back(std::move(j));
        }
        out["contextProjections"] = std::move(list);
    }

    Json points = Json::array();
    for (const auto& p : sketch.points) {
        Json j{{"id", ToJson(p.id)}, {"pos", ToJson(p.pos)}};
        if (p.isOrigin) j["origin"] = true;
        if (p.projected) j["projected"] = true;
        points.push_back(std::move(j));
    }
    Json curves = Json::array();
    for (const auto& l : sketch.lines)
        curves.push_back({{"id", ToJson(l.id)}, {"type", "line"}, {"start", ToJson(l.start)},
                          {"end", ToJson(l.end)}, {"construction", l.construction}, {"centerline", l.centerline},
                          {"projected", l.projected}});
    for (const auto& c : sketch.circles)
        curves.push_back({{"id", ToJson(c.id)}, {"type", "circle"}, {"center", ToJson(c.center)},
                          {"radius", c.radius}, {"construction", c.construction}, {"projected", c.projected}});
    for (const auto& a : sketch.arcs)
        curves.push_back({{"id", ToJson(a.id)}, {"type", "arc"}, {"center", ToJson(a.center)},
                          {"start", ToJson(a.start)}, {"end", ToJson(a.end)}, {"construction", a.construction},
                          {"projected", a.projected}});
    for (const auto& s : sketch.splines) {
        Json pts = Json::array();
        for (ObjectId p : s.points) pts.push_back(ToJson(p));
        curves.push_back({{"id", ToJson(s.id)}, {"type", "spline"}, {"points", std::move(pts)},
                          {"interpolate", s.interpolate}, {"construction", s.construction}});
    }
    for (const auto& e : sketch.ellipses)
        curves.push_back({{"id", ToJson(e.id)}, {"type", "ellipse"}, {"center", ToJson(e.center)}, {"major", ToJson(e.major)},
                          {"minor", ToJson(e.minor)}, {"construction", e.construction}});
    for (const auto& t : sketch.texts) {
        Json corners = Json::array(), sides = Json::array();
        for (ObjectId p : t.corners) corners.push_back(ToJson(p));
        for (ObjectId l : t.sides) sides.push_back(ToJson(l));
        curves.push_back({{"id", ToJson(t.id)}, {"type", "text"}, {"corners", std::move(corners)}, {"sides", std::move(sides)},
                          {"text", t.text}, {"height", t.height}, {"font", t.font}, {"bold", t.bold}, {"italic", t.italic},
                          {"halign", t.hAlign}, {"valign", t.vAlign}, {"construction", t.construction}});
    }

    Json constraints = Json::array();
    for (const auto& c : sketch.constraints) {
        Json j{{"id", ToJson(c.id)}, {"type", ConstraintTypeId(c.type)}, {"refs", Json::array()}};
        for (ObjectId r : c.refs)
            if (r) j["refs"].push_back(ToJson(r));
        if (sketch::IsDimensional(c.type)) {
            j["parameter"] = ToJson(c.parameter);
            j["labelOffset"] = ToJson(c.labelOffset);
            if (c.reference) j["reference"] = true;
        }
        if (c.type == ConstraintType::Fix) j["fixedAt"] = ToJson(c.fixedAt);
        constraints.push_back(std::move(j));
    }

    out["points"] = std::move(points);
    out["curves"] = std::move(curves);
    out["constraints"] = std::move(constraints);
}

Status SketchFeature::LoadFields(const Json& in) {
    SketchPlacement pl;
    if (const auto it = in.find("placement"); it != in.end()) {
        if (!ReadMember(*it, "support", pl.support) || !ReadMember(*it, "frame", pl.frame) ||
            !ReadMember(*it, "onFace", pl.onFace) || !ReadMember(*it, "face", pl.face))
            return Bad("placement");
    }

    sketch::Sketch sk;
    for (const Json& j : in.value("points", Json::array())) {
        sketch::Point p;
        if (!ReadMember(j, "id", p.id) || !ReadMember(j, "pos", p.pos) || !ReadMember(j, "origin", p.isOrigin) ||
            !ReadMember(j, "projected", p.projected))
            return Bad("points");
        sk.points.push_back(p);
    }
    for (const Json& j : in.value("curves", Json::array())) {
        const std::string type = j.value("type", "");
        bool ok = true;
        if (type == "line") {
            sketch::Line l;
            ok = ReadMember(j, "id", l.id) && ReadMember(j, "start", l.start) && ReadMember(j, "end", l.end) &&
                 ReadMember(j, "construction", l.construction) && ReadMember(j, "centerline", l.centerline) &&
                 ReadMember(j, "projected", l.projected);
            sk.lines.push_back(l);
        } else if (type == "circle") {
            sketch::Circle c;
            ok = ReadMember(j, "id", c.id) && ReadMember(j, "center", c.center) && ReadMember(j, "radius", c.radius) &&
                 ReadMember(j, "construction", c.construction) && ReadMember(j, "projected", c.projected);
            sk.circles.push_back(c);
        } else if (type == "arc") {
            sketch::Arc a;
            ok = ReadMember(j, "id", a.id) && ReadMember(j, "center", a.center) && ReadMember(j, "start", a.start) &&
                 ReadMember(j, "end", a.end) && ReadMember(j, "construction", a.construction) &&
                 ReadMember(j, "projected", a.projected);
            sk.arcs.push_back(a);
        } else if (type == "spline") {
            sketch::Spline s;
            ok = ReadMember(j, "id", s.id) && ReadMember(j, "interpolate", s.interpolate) &&
                 ReadMember(j, "construction", s.construction);
            for (const Json& p : j.value("points", Json::array())) {
                ObjectId point;
                ok = ok && json_io::Read(p, point);
                s.points.push_back(point);
            }
            ok = ok && s.points.size() >= 2;
            sk.splines.push_back(std::move(s));
        } else if (type == "ellipse") {
            sketch::Ellipse e;
            ok = ReadMember(j, "id", e.id) && ReadMember(j, "center", e.center) && ReadMember(j, "major", e.major) &&
                 ReadMember(j, "minor", e.minor) && ReadMember(j, "construction", e.construction);
            sk.ellipses.push_back(e);
        } else if (type == "text") {
            sketch::Text t;
            ok = ReadMember(j, "id", t.id) && ReadMember(j, "text", t.text) && ReadMember(j, "height", t.height) &&
                 ReadMember(j, "font", t.font) && ReadMember(j, "bold", t.bold) && ReadMember(j, "italic", t.italic) &&
                 ReadMember(j, "halign", t.hAlign) && ReadMember(j, "valign", t.vAlign) &&
                 ReadMember(j, "construction", t.construction) && t.height > 0 && t.hAlign < 3 && t.vAlign < 3;
            const Json corners = j.value("corners", Json::array()), sides = j.value("sides", Json::array());
            ok = ok && corners.size() == 4 && sides.size() == 4;
            for (std::size_t i = 0; ok && i < 4; ++i) ok = json_io::Read(corners[i], t.corners[i]) && json_io::Read(sides[i], t.sides[i]);
            sk.texts.push_back(std::move(t));
        }
        // Unknown curve types from newer versions are skipped, not fatal.
        if (!ok) return Bad("curves");
    }
    for (const Json& j : in.value("constraints", Json::array())) {
        sketch::Constraint c;
        const std::string type = j.value("type", "");
        if (!ConstraintTypeFromId(type, c.type)) continue;   // newer constraint type
        if (!ReadMember(j, "id", c.id)) return Bad("constraints");
        const Json refs = j.value("refs", Json::array());
        for (std::size_t i = 0; i < refs.size() && i < 3; ++i)
            if (!json_io::Read(refs[i], c.refs[i])) return Bad("constraints.refs");
        if (!ReadMember(j, "parameter", c.parameter) || !ReadMember(j, "labelOffset", c.labelOffset) ||
            !ReadMember(j, "reference", c.reference) || !ReadMember(j, "fixedAt", c.fixedAt))
            return Bad("constraints");
        sk.constraints.push_back(std::move(c));
    }

    std::vector<Projection> projected;
    for (const Json& j : in.value("projections", Json::array())) {
        Projection p;
        if (!j.is_object() || !ReadMember(j, "curve", p.curve) || !ReadSource(j, p.source)) return Bad("projections");
        const std::string of = j.value("of", std::string());
        if (of == "vertex") p.of = Projection::Of::Vertex;
        else if (of == "work") p.of = Projection::Of::Work;
        else if (of == "sketch") p.of = Projection::Of::Sketch;
        else if (of == "flat") p.of = Projection::Of::Flat;
        else if (!of.empty()) return Bad("projections");
        const bool byEdge = p.of == Projection::Of::Edge || p.of == Projection::Of::Vertex;
        const auto edge = j.find("edge");
        if (byEdge && (edge == j.end() || !json_io::Read(*edge, p.edge))) return Bad("projections");
        if (p.of == Projection::Of::Vertex) {
            const auto at = j.find("at");
            if (at == j.end() || !json_io::Read(*at, p.at)) return Bad("projections");
        }
        if ((p.of == Projection::Of::Work || p.of == Projection::Of::Flat) && !ReadMember(j, "work", p.work)) return Bad("projections");
        if (p.of == Projection::Of::Flat && !ReadMember(j, "part", p.part)) return Bad("projections");
        if (p.of == Projection::Of::Sketch && !p.source.IsSet()) return Bad("projections");
        projected.push_back(p);
    }

    // Edges of other parts of an assembly (absent in older files).
    std::vector<ContextProjection> fromAssembly;
    for (const Json& j : in.value("contextProjections", Json::array())) {
        ContextProjection p;
        const auto edge = j.find("edge");
        if (!j.is_object() || edge == j.end() || !json_io::Read(*edge, p.edge) || !ReadMember(j, "curve", p.curve) ||
            !ReadMember(j, "assembly", p.assembly) || !ReadMember(j, "component", p.component) ||
            !ReadMember(j, "adaptive", p.adaptive) || !ReadSource(j, p.source))
            return Bad("contextProjections");
        fromAssembly.push_back(std::move(p));
    }

    placement = pl;
    projections = std::move(projected);
    contextProjections = std::move(fromAssembly);
    sketch    = std::move(sk);
    return Status::Ok();
}

} // namespace bz::doc
