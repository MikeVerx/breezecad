#include "document/EdgeFeature.h"

#include "document/JsonIO.h"

#include <array>
#include <utility>

namespace bz::doc {
namespace {

using json_io::FromId;
using json_io::IdOf;
using json_io::Json;

constexpr std::array<std::pair<CurveKind, std::string_view>, 3> kCurveIds{{
    {CurveKind::Line, "line"}, {CurveKind::Circle, "circle"}, {CurveKind::Other, "other"}}};

constexpr std::array<std::pair<SurfaceKind, std::string_view>, 6> kSurfaceIds{{
    {SurfaceKind::Plane, "plane"},   {SurfaceKind::Cylinder, "cylinder"}, {SurfaceKind::Cone, "cone"},
    {SurfaceKind::Sphere, "sphere"}, {SurfaceKind::Torus, "torus"},       {SurfaceKind::Other, "other"}}};

Json ToJson(const EdgeReference& e) {
    Json faces = Json::array();
    for (const FaceSignature& f : e.faces)
        faces.push_back(Json{{"kind", IdOf(kSurfaceIds, f.kind)}, {"direction", json_io::ToJson(f.direction)}});
    return Json{{"curve", IdOf(kCurveIds, e.curve)},
                {"direction", json_io::ToJson(e.direction)},
                {"radius", e.radius},
                {"middle", json_io::ToJson(e.middle)},
                {"faces", std::move(faces)}};
}

bool Read(const Json& j, EdgeReference& out) {
    EdgeReference e;
    std::string curve;
    if (!j.is_object() || !json_io::ReadMember(j, "curve", curve) || !FromId(kCurveIds, curve, e.curve) ||
        !json_io::ReadMember(j, "direction", e.direction) || !json_io::ReadMember(j, "radius", e.radius) ||
        !json_io::ReadMember(j, "middle", e.middle))
        return false;
    const auto faces = j.find("faces");
    if (faces == j.end() || !faces->is_array() || faces->size() != 2) return false;
    for (std::size_t i = 0; i < 2; ++i) {
        std::string kind;
        if (!json_io::ReadMember((*faces)[i], "kind", kind) || !FromId(kSurfaceIds, kind, e.faces[i].kind) ||
            !json_io::ReadMember((*faces)[i], "direction", e.faces[i].direction))
            return false;
    }
    out = e;
    return true;
}

} // namespace

void EdgeFeature::SaveEdges(Json& out) const {
    Json list = Json::array();
    for (const EdgeReference& e : edges) list.push_back(ToJson(e));
    out["edges"] = std::move(list);
}

Status EdgeFeature::LoadEdges(const Json& in, const std::string& what) {
    std::vector<EdgeReference> read;
    const auto it = in.find("edges");
    if (it == in.end() || !it->is_array()) return Error{what + ": нет списка рёбер"};
    for (const Json& j : *it) {
        EdgeReference e;
        if (!Read(j, e)) return Error{what + ": повреждено описание ребра"};
        read.push_back(e);
    }
    edges = std::move(read);
    return Status::Ok();
}

void FilletFeature::SaveFields(Json& out) const {
    SaveEdges(out);
    out["radius"] = json_io::ToJson(radius);
}

Status FilletFeature::LoadFields(const Json& in) {
    FilletFeature f;
    if (Status s = f.LoadEdges(in, "Сопряжение"); !s) return s;
    if (!json_io::ReadMember(in, "radius", f.radius)) return Error{"Сопряжение: повреждён радиус"};
    edges = std::move(f.edges);
    radius = std::move(f.radius);
    return Status::Ok();
}

void ChamferFeature::SaveFields(Json& out) const {
    SaveEdges(out);
    out["distance"] = json_io::ToJson(distance);
}

Status ChamferFeature::LoadFields(const Json& in) {
    ChamferFeature f;
    if (Status s = f.LoadEdges(in, "Фаска"); !s) return s;
    if (!json_io::ReadMember(in, "distance", f.distance)) return Error{"Фаска: повреждено расстояние"};
    edges = std::move(f.edges);
    distance = std::move(f.distance);
    return Status::Ok();
}

} // namespace bz::doc
