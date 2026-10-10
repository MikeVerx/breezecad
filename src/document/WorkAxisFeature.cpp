#include "document/WorkAxisFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

#include <array>
#include <format>
#include <string>

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

namespace {

constexpr std::array<std::pair<WorkAxisMethod, std::string_view>, 4> kMethodIds{{
    {WorkAxisMethod::Edge, "edge"},
    {WorkAxisMethod::Cylinder, "cylinder"},
    {WorkAxisMethod::Planes, "planes"},
    {WorkAxisMethod::Points, "points"},
}};

} // namespace

std::vector<ObjectId> WorkAxisFeature::Dependencies() const {
    std::vector<ObjectId> out;
    if (method == WorkAxisMethod::Points)
        for (const ObjectId w : pointFeatures)
            if (w) out.push_back(w);
    if (method != WorkAxisMethod::Planes) return out;
    for (const ObjectId p : {plane1, plane2})
        if (p && !origin::IsOriginElement(p)) out.push_back(p);
    return out;
}

void WorkAxisFeature::SaveFields(Json& out) const {
    out["method"] = std::string(json_io::IdOf(kMethodIds, method));
    if (edge) out["edge"] = json_io::ToJson(*edge);
    if (cylinder)
        out["cylinder"] = {{"point", json_io::ToJson(cylinder->axisPoint)}, {"axis", json_io::ToJson(cylinder->axisDir)},
                           {"radius", cylinder->radius}};
    out["plane1"] = json_io::ToJson(plane1);
    out["plane2"] = json_io::ToJson(plane2);
    if (method == WorkAxisMethod::Points) json_io::SavePointPicks(out, points, pointEdges, pointFeatures);
}

Status WorkAxisFeature::LoadFields(const Json& in) {
    std::string methodId = "cylinder";
    ObjectId p1, p2;
    if (!ReadMember(in, "method", methodId) || !ReadMember(in, "plane1", p1) || !ReadMember(in, "plane2", p2))
        return Error{"Рабочая ось: повреждены параметры"};
    WorkAxisMethod m;
    if (!json_io::FromId(kMethodIds, methodId, m)) return Error{std::format("Рабочая ось: неизвестный способ «{}»", methodId)};
    std::optional<EdgeReference> e;
    if (const auto it = in.find("edge"); it != in.end()) {
        EdgeReference read;
        if (!json_io::Read(*it, read)) return Error{"Рабочая ось: повреждено ребро"};
        e = read;
    }
    std::optional<CylinderTouch> c;
    if (const auto it = in.find("cylinder"); it != in.end()) {
        CylinderTouch t;
        if (!it->is_object() || !json_io::Read(it->value("point", Json()), t.axisPoint) ||
            !json_io::Read(it->value("axis", Json()), t.axisDir) || !it->contains("radius") || !(*it)["radius"].is_number())
            return Error{"Рабочая ось: повреждён цилиндр"};
        t.radius = (*it)["radius"].get<double>();
        c = t;
    }
    std::vector<Vec3> pts;
    std::vector<std::optional<EdgeReference>> ptEdges;
    std::vector<ObjectId> ptFeatures;
    if (!json_io::LoadPointPicks(in, pts, ptEdges, ptFeatures)) return Error{"Рабочая ось: повреждены точки"};
    method = m;
    edge = e;
    points = std::move(pts);
    pointEdges = std::move(ptEdges);
    pointFeatures = std::move(ptFeatures);
    cylinder = c;
    plane1 = p1;
    plane2 = p2;
    return Status::Ok();
}

} // namespace bz::doc
