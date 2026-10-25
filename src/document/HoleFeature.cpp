#include "document/HoleFeature.h"

#include "document/JsonIO.h"

#include <array>
#include <format>
#include <utility>

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;

constexpr std::array<std::pair<HoleType, std::string_view>, 3> kTypeIds{{
    {HoleType::Simple, "simple"}, {HoleType::Counterbore, "counterbore"}, {HoleType::Countersink, "countersink"}}};

} // namespace

void HoleFeature::RemoveCentre(std::size_t index) {
    if (index >= centres.size()) return;
    centres.erase(centres.begin() + static_cast<std::ptrdiff_t>(index));
    std::erase_if(concentric, [&](const HoleAxis& a) { return a.centre == index; });
    for (HoleAxis& a : concentric)
        if (a.centre > index) --a.centre;
}

const HoleAxis* HoleFeature::AxisOf(std::size_t index) const {
    for (const HoleAxis& a : concentric)
        if (a.centre == index) return &a;
    return nullptr;
}

void HoleFeature::CollectParameters(std::vector<Parameter*>& out) {
    out.push_back(&diameter);
    out.push_back(&depth);
    out.push_back(&headDiameter);
    out.push_back(&headDepth);
    out.push_back(&sinkDiameter);
}

void HoleFeature::SaveFields(Json& out) const {
    Json centreList = Json::array();
    for (Vec2 c : centres) centreList.push_back(json_io::ToJson(c));
    const std::string_view typeId = json_io::IdOf(kTypeIds, type);
    out["face"]         = json_io::ToJson(face);
    out["centres"]      = std::move(centreList);
    out["holeType"]     = typeId;
    out["throughAll"]   = throughAll;
    out["diameter"]     = json_io::ToJson(diameter);
    out["depth"]        = json_io::ToJson(depth);
    out["headDiameter"] = json_io::ToJson(headDiameter);
    out["headDepth"]    = json_io::ToJson(headDepth);
    out["sinkDiameter"] = json_io::ToJson(sinkDiameter);
    if (!concentric.empty()) {
        Json axes = Json::array();
        for (const HoleAxis& a : concentric) {
            Json j{{"centre", a.centre}};
            if (a.edge) j["edge"] = json_io::ToJson(*a.edge);
            if (a.cylinder)
                j["cylinder"] = {{"point", json_io::ToJson(a.cylinder->axisPoint)}, {"axis", json_io::ToJson(a.cylinder->axisDir)},
                                 {"radius", a.cylinder->radius}};
            axes.push_back(std::move(j));
        }
        out["concentric"] = std::move(axes);
    }
}

Status HoleFeature::LoadFields(const Json& in) {
    HoleFeature f;
    std::string typeId = "simple";
    if (!ReadMember(in, "face", f.face) || !ReadMember(in, "holeType", typeId) || !ReadMember(in, "throughAll", f.throughAll) ||
        !ReadMember(in, "diameter", f.diameter) || !ReadMember(in, "depth", f.depth) || !ReadMember(in, "headDiameter", f.headDiameter) ||
        !ReadMember(in, "headDepth", f.headDepth) || !ReadMember(in, "sinkDiameter", f.sinkDiameter))
        return Error{"Отверстие: повреждены параметры"};
    if (!json_io::FromId(kTypeIds, typeId, f.type)) return Error{std::format("Отверстие: неизвестный тип «{}»", typeId)};
    for (const Json& c : in.value("centres", Json::array())) {
        Vec2 v;
        if (!json_io::Read(c, v)) return Error{"Отверстие: повреждены центры"};
        f.centres.push_back(v);
    }
    for (const Json& j : in.value("concentric", Json::array())) {
        HoleAxis a;
        if (!j.is_object() || !j.contains("centre") || !j["centre"].is_number_unsigned()) return Error{"Отверстие: повреждена соосность"};
        a.centre = j["centre"].get<std::size_t>();
        if (const auto it = j.find("edge"); it != j.end()) {
            EdgeReference e;
            if (!json_io::Read(*it, e)) return Error{"Отверстие: повреждена кромка соосности"};
            a.edge = e;
        }
        if (const auto it = j.find("cylinder"); it != j.end()) {
            CylinderTouch t;
            if (!it->is_object() || !json_io::Read(it->value("point", Json()), t.axisPoint) ||
                !json_io::Read(it->value("axis", Json()), t.axisDir) || !it->contains("radius") || !(*it)["radius"].is_number())
                return Error{"Отверстие: повреждён цилиндр соосности"};
            t.radius = (*it)["radius"].get<double>();
            a.cylinder = t;
        }
        if (a.centre >= f.centres.size() || (!a.edge && !a.cylinder)) continue;   // nothing to hold: a free centre
        f.concentric.push_back(a);
    }
    // Only the type's own fields: id, name and suppressed are already read.
    face         = f.face;
    centres      = std::move(f.centres);
    concentric   = std::move(f.concentric);
    type         = f.type;
    throughAll   = f.throughAll;
    diameter     = std::move(f.diameter);
    depth        = std::move(f.depth);
    headDiameter = std::move(f.headDiameter);
    headDepth    = std::move(f.headDepth);
    sinkDiameter = std::move(f.sinkDiameter);
    return Status::Ok();
}

} // namespace bz::doc
