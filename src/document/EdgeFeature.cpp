#include "document/EdgeFeature.h"

#include "document/JsonIO.h"

#include <array>
#include <utility>

namespace bz::doc {
using json_io::Json;

void EdgeFeature::SaveEdges(Json& out) const {
    Json list = Json::array();
    for (const EdgeReference& e : edges) list.push_back(json_io::ToJson(e));
    out["edges"] = std::move(list);
}

Status EdgeFeature::LoadEdges(const Json& in, const std::string& what) {
    std::vector<EdgeReference> read;
    const auto it = in.find("edges");
    if (it == in.end() || !it->is_array()) return Error{what + ": нет списка рёбер"};
    for (const Json& j : *it) {
        EdgeReference e;
        if (!json_io::Read(j, e)) return Error{what + ": повреждено описание ребра"};
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
