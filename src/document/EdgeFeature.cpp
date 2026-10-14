#include "document/EdgeFeature.h"

#include "document/JsonIO.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
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
    if (it == in.end() || !it->is_array()) return Error{std::format("{}: нет списка рёбер", what)};
    for (const Json& j : *it) {
        EdgeReference e;
        if (!json_io::Read(j, e)) return Error{std::format("{}: повреждено описание ребра", what)};
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

namespace {

constexpr std::array<std::pair<ChamferMode, std::string_view>, 3> kChamferModes{{
    {ChamferMode::EqualDistance, "equal"},
    {ChamferMode::DistanceAngle, "distanceAngle"},
    {ChamferMode::TwoDistances, "twoDistances"},
}};

} // namespace

bool ChamferFeature::FlippedAt(std::size_t index) const {
    const bool own = index < edges.size() && std::ranges::find(flippedEdges, edges[index]) != flippedEdges.end();
    return flipped != own;
}

void ChamferFeature::FlipAt(std::size_t index) {
    if (index >= edges.size()) return;
    // Entries of edges no longer picked go too: the list stays what it says.
    std::erase_if(flippedEdges, [&](const EdgeReference& e) { return std::ranges::find(edges, e) == edges.end(); });
    if (const auto it = std::ranges::find(flippedEdges, edges[index]); it != flippedEdges.end()) flippedEdges.erase(it);
    else flippedEdges.push_back(edges[index]);
}

void ChamferFeature::SaveFields(Json& out) const {
    SaveEdges(out);
    out["distance"] = json_io::ToJson(distance);
    // Added in 0.2.2; older files have none of these: equal distances.
    for (const auto& [m, label] : kChamferModes)
        if (m == mode) out["mode"] = label;
    out["distance2"] = json_io::ToJson(distance2);
    out["angle"] = json_io::ToJson(angle);
    out["flipped"] = flipped;
    if (!flippedEdges.empty()) {
        Json list = Json::array();
        for (const EdgeReference& e : flippedEdges) list.push_back(json_io::ToJson(e));
        out["flippedEdges"] = std::move(list);
    }
}

Status ChamferFeature::LoadFields(const Json& in) {
    ChamferFeature f;
    if (Status s = f.LoadEdges(in, "Фаска"); !s) return s;
    std::string modeName = "equal";
    if (!json_io::ReadMember(in, "distance", f.distance) || !json_io::ReadMember(in, "mode", modeName) ||
        !json_io::ReadMember(in, "distance2", f.distance2) || !json_io::ReadMember(in, "angle", f.angle) ||
        !json_io::ReadMember(in, "flipped", f.flipped))
        return Error{"Фаска: повреждены размеры"};
    for (const Json& j : in.value("flippedEdges", Json::array())) {
        EdgeReference e;
        if (!json_io::Read(j, e)) return Error{"Фаска: повреждено ребро смены стороны"};
        f.flippedEdges.push_back(e);
    }
    const auto known = std::ranges::find(kChamferModes, modeName, [](const auto& m) { return m.second; });
    if (known == kChamferModes.end()) return Error{std::format("Фаска: неизвестный способ «{}»", modeName)};
    edges = std::move(f.edges);
    mode = known->first;
    distance = std::move(f.distance);
    distance2 = std::move(f.distance2);
    angle = std::move(f.angle);
    flipped = f.flipped;
    flippedEdges = std::move(f.flippedEdges);
    return Status::Ok();
}

} // namespace bz::doc
