#include "document/SketchFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

#include <array>
#include <utility>

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;
using json_io::ToJson;
using sketch::ConstraintType;

constexpr std::array<std::pair<ConstraintType, std::string_view>, 19> kConstraintIds{{
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
    {ConstraintType::Distance, "distance"},
    {ConstraintType::DistanceX, "distanceX"},
    {ConstraintType::DistanceY, "distanceY"},
    {ConstraintType::Length, "length"},
    {ConstraintType::PointLineDistance, "pointLineDistance"},
    {ConstraintType::Radius, "radius"},
    {ConstraintType::Diameter, "diameter"},
    {ConstraintType::Angle, "angle"},
}};

Error Bad(std::string_view what) { return Error{"Эскиз: повреждено поле «" + std::string(what) + "»"}; }

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
    for (const auto& c : sketch.constraints) ids.Reserve(c.id);
}

void SketchFeature::PruneProjections() {
    std::erase_if(projections, [&](const Projection& p) { return sketch.KindOf(p.curve) == sketch::EntityKind::None; });
}

void SketchFeature::SaveFields(Json& out) const {
    out["placement"] = Json{{"support", ToJson(placement.support)},
                            {"frame", ToJson(placement.frame)},
                            {"onFace", placement.onFace}};
    if (placement.face.IsSet()) out["placement"]["face"] = json_io::ToJson(placement.face);
    if (!projections.empty()) {
        Json list = Json::array();
        for (const Projection& p : projections) list.push_back({{"edge", json_io::ToJson(p.edge)}, {"curve", ToJson(p.curve)}});
        out["projections"] = std::move(list);
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
                          {"end", ToJson(l.end)}, {"construction", l.construction}, {"projected", l.projected}});
    for (const auto& c : sketch.circles)
        curves.push_back({{"id", ToJson(c.id)}, {"type", "circle"}, {"center", ToJson(c.center)},
                          {"radius", c.radius}, {"construction", c.construction}, {"projected", c.projected}});
    for (const auto& a : sketch.arcs)
        curves.push_back({{"id", ToJson(a.id)}, {"type", "arc"}, {"center", ToJson(a.center)},
                          {"start", ToJson(a.start)}, {"end", ToJson(a.end)}, {"construction", a.construction},
                          {"projected", a.projected}});

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
                 ReadMember(j, "construction", l.construction) && ReadMember(j, "projected", l.projected);
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
        for (std::size_t i = 0; i < refs.size() && i < 2; ++i)
            if (!json_io::Read(refs[i], c.refs[i])) return Bad("constraints.refs");
        if (!ReadMember(j, "parameter", c.parameter) || !ReadMember(j, "labelOffset", c.labelOffset) ||
            !ReadMember(j, "reference", c.reference) || !ReadMember(j, "fixedAt", c.fixedAt))
            return Bad("constraints");
        sk.constraints.push_back(std::move(c));
    }

    std::vector<Projection> projected;
    for (const Json& j : in.value("projections", Json::array())) {
        Projection p;
        const auto edge = j.find("edge");
        if (edge == j.end() || !json_io::Read(*edge, p.edge) || !ReadMember(j, "curve", p.curve)) return Bad("projections");
        projected.push_back(p);
    }

    placement = pl;
    projections = std::move(projected);
    sketch    = std::move(sk);
    return Status::Ok();
}

} // namespace bz::doc
