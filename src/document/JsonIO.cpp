#include "document/JsonIO.h"

namespace bz::doc::json_io {

std::string_view QuantityId(Quantity q) {
    switch (q) {
        case Quantity::Length: return "length";
        case Quantity::Angle:  return "angle";
        case Quantity::Scalar: return "scalar";
    }
    return "scalar";
}

Quantity QuantityFromId(std::string_view id) {
    if (id == "length") return Quantity::Length;
    if (id == "angle") return Quantity::Angle;
    return Quantity::Scalar;
}

Json ToJson(const Parameter& p) {
    // `value` is a cache for readers that cannot evaluate expressions.
    return Json{{"name", p.name}, {"expression", p.expression},
                {"quantity", QuantityId(p.quantity)}, {"value", p.value}};
}

bool Read(const Json& j, Vec2& out) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return false;
    out = {j[0].get<double>(), j[1].get<double>()};
    return true;
}

bool Read(const Json& j, FaceReference& out) {
    if (!j.is_object() || !j.contains("normal") || !j.contains("point") || !Read(j["normal"], out.normal) || !Read(j["point"], out.point))
        return false;
    const auto curved = j.find("curved");
    out.curved = curved != j.end() && curved->is_boolean() && curved->get<bool>();
    return true;
}

bool Read(const Json& j, Vec3& out) {
    if (!j.is_array() || j.size() != 3) return false;
    for (const auto& e : j)
        if (!e.is_number()) return false;
    out = {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
    return true;
}

bool Read(const Json& j, ObjectId& out) {
    if (!j.is_number_unsigned()) return false;
    out = ObjectId{j.get<std::uint64_t>()};
    return true;
}

bool Read(const Json& j, Frame& out) {
    if (!j.is_object()) return false;
    Frame f;
    if (!ReadMember(j, "origin", f.origin) || !ReadMember(j, "xDir", f.xDir) || !ReadMember(j, "normal", f.normal))
        return false;
    f.xDir   = f.xDir.Normalized();
    f.normal = f.normal.Normalized();
    if (f.xDir.Length() == 0.0 || f.normal.Length() == 0.0) return false;
    out = f;
    return true;
}

bool Read(const Json& j, Parameter& out) {
    if (!j.is_object()) return false;
    Parameter p;
    std::string quantity = "length";
    if (!ReadMember(j, "name", p.name) || !ReadMember(j, "expression", p.expression) ||
        !ReadMember(j, "quantity", quantity) || !ReadMember(j, "value", p.value))
        return false;
    p.quantity = QuantityFromId(quantity);
    out = std::move(p);
    return true;
}

namespace {

constexpr std::array<std::pair<CurveKind, std::string_view>, 3> kCurveIds{{
    {CurveKind::Line, "line"}, {CurveKind::Circle, "circle"}, {CurveKind::Other, "other"}}};

constexpr std::array<std::pair<SurfaceKind, std::string_view>, 6> kSurfaceIds{{
    {SurfaceKind::Plane, "plane"},   {SurfaceKind::Cylinder, "cylinder"}, {SurfaceKind::Cone, "cone"},
    {SurfaceKind::Sphere, "sphere"}, {SurfaceKind::Torus, "torus"},       {SurfaceKind::Other, "other"}}};

} // namespace

Json ToJson(const EdgeReference& e) {
    Json faces = Json::array();
    for (const FaceSignature& f : e.faces)
        faces.push_back(Json{{"kind", IdOf(kSurfaceIds, f.kind)}, {"direction", ToJson(f.direction)}});
    return Json{{"curve", IdOf(kCurveIds, e.curve)},
                {"direction", ToJson(e.direction)},
                {"radius", e.radius},
                {"middle", ToJson(e.middle)},
                {"faces", std::move(faces)}};
}

bool Read(const Json& j, EdgeReference& out) {
    EdgeReference e;
    std::string curve;
    if (!j.is_object() || !ReadMember(j, "curve", curve) || !FromId(kCurveIds, curve, e.curve) ||
        !ReadMember(j, "direction", e.direction) || !ReadMember(j, "radius", e.radius) ||
        !ReadMember(j, "middle", e.middle))
        return false;
    const auto faces = j.find("faces");
    if (faces == j.end() || !faces->is_array() || faces->size() != 2) return false;
    for (std::size_t i = 0; i < 2; ++i) {
        std::string kind;
        if (!ReadMember((*faces)[i], "kind", kind) || !FromId(kSurfaceIds, kind, e.faces[i].kind) ||
            !ReadMember((*faces)[i], "direction", e.faces[i].direction))
            return false;
    }
    out = e;
    return true;
}

void SavePointPicks(Json& out, const std::vector<Vec3>& points, const std::vector<std::optional<EdgeReference>>& edges,
                    const std::vector<ObjectId>& workPoints) {
    Json pts = Json::array(), es = Json::array(), ws = Json::array();
    for (std::size_t i = 0; i < points.size(); ++i) {
        pts.push_back(ToJson(points[i]));
        es.push_back(i < edges.size() && edges[i] ? ToJson(*edges[i]) : Json());
        ws.push_back(ToJson(i < workPoints.size() ? workPoints[i] : ObjectId{}));
    }
    out["points"] = std::move(pts);
    out["pointEdges"] = std::move(es);
    out["pointFeatures"] = std::move(ws);
}

bool LoadPointPicks(const Json& in, std::vector<Vec3>& points, std::vector<std::optional<EdgeReference>>& edges,
                    std::vector<ObjectId>& workPoints) {
    std::vector<Vec3> pts;
    if (const auto it = in.find("points"); it != in.end()) {
        if (!it->is_array()) return false;
        for (const Json& p : *it) {
            Vec3 v;
            if (!Read(p, v)) return false;
            pts.push_back(v);
        }
    }
    std::vector<std::optional<EdgeReference>> es(pts.size());
    if (const auto it = in.find("pointEdges"); it != in.end() && it->is_array())
        for (std::size_t i = 0; i < it->size() && i < es.size(); ++i)
            if (EdgeReference ref; !(*it)[i].is_null() && Read((*it)[i], ref)) es[i] = ref;
    std::vector<ObjectId> ws(pts.size());
    if (const auto it = in.find("pointFeatures"); it != in.end() && it->is_array())
        for (std::size_t i = 0; i < it->size() && i < ws.size(); ++i)
            if (ObjectId id; Read((*it)[i], id)) ws[i] = id;
    points = std::move(pts);
    edges = std::move(es);
    workPoints = std::move(ws);
    return true;
}

} // namespace bz::doc::json_io
