#include "document/WorkPointFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

#include <array>
#include <format>
#include <string>
#include <utility>

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

namespace {

constexpr std::array<std::pair<WorkPointMethod, std::string_view>, 5> kMethodIds{{
    {WorkPointMethod::Vertex, "vertex"},
    {WorkPointMethod::Center, "center"},
    {WorkPointMethod::Midpoint, "midpoint"},
    {WorkPointMethod::Intersection, "intersection"},
    {WorkPointMethod::Coordinates, "coordinates"},
}};

} // namespace

std::vector<ObjectId> WorkPointFeature::Dependencies() const {
    std::vector<ObjectId> out;
    if (method != WorkPointMethod::Intersection) return out;
    if (axis) out.push_back(axis);
    if (plane && !origin::IsOriginElement(plane)) out.push_back(plane);
    return out;
}

void WorkPointFeature::SaveFields(Json& out) const {
    out["method"] = std::string(json_io::IdOf(kMethodIds, method));
    out["vertex"] = json_io::ToJson(vertex);
    if (edge) out["edge"] = json_io::ToJson(*edge);
    if (sphere) out["sphere"] = json_io::ToJson(*sphere);
    out["axis"] = json_io::ToJson(axis);
    out["plane"] = json_io::ToJson(plane);
    out["x"] = json_io::ToJson(x);
    out["y"] = json_io::ToJson(y);
    out["z"] = json_io::ToJson(z);
}

Status WorkPointFeature::LoadFields(const Json& in) {
    WorkPointFeature f;
    std::string methodId = "vertex";
    if (!ReadMember(in, "method", methodId) || !ReadMember(in, "vertex", f.vertex) || !ReadMember(in, "axis", f.axis) ||
        !ReadMember(in, "plane", f.plane) || !ReadMember(in, "x", f.x) || !ReadMember(in, "y", f.y) || !ReadMember(in, "z", f.z))
        return Error{"Рабочая точка: повреждены параметры"};
    if (!json_io::FromId(kMethodIds, methodId, f.method))
        return Error{std::format("Рабочая точка: неизвестный способ «{}»", methodId)};
    if (const auto it = in.find("edge"); it != in.end()) {
        EdgeReference e;
        if (!json_io::Read(*it, e)) return Error{"Рабочая точка: повреждено ребро"};
        f.edge = e;
    }
    if (const auto it = in.find("sphere"); it != in.end()) {
        FaceReference s;
        if (!json_io::Read(*it, s)) return Error{"Рабочая точка: повреждена сфера"};
        f.sphere = s;
    }
    method = f.method;
    vertex = f.vertex;
    edge = std::move(f.edge);
    sphere = std::move(f.sphere);
    axis = f.axis;
    plane = f.plane;
    x = std::move(f.x);
    y = std::move(f.y);
    z = std::move(f.z);
    return Status::Ok();
}

} // namespace bz::doc
