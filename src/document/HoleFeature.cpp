#include "document/HoleFeature.h"

#include "document/JsonIO.h"

#include <array>
#include <utility>

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;

constexpr std::array<std::pair<HoleType, std::string_view>, 3> kTypeIds{{
    {HoleType::Simple, "simple"}, {HoleType::Counterbore, "counterbore"}, {HoleType::Countersink, "countersink"}}};

} // namespace

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
    out["type"]         = typeId;
    out["throughAll"]   = throughAll;
    out["diameter"]     = json_io::ToJson(diameter);
    out["depth"]        = json_io::ToJson(depth);
    out["headDiameter"] = json_io::ToJson(headDiameter);
    out["headDepth"]    = json_io::ToJson(headDepth);
    out["sinkDiameter"] = json_io::ToJson(sinkDiameter);
}

Status HoleFeature::LoadFields(const Json& in) {
    HoleFeature f;
    std::string typeId = "simple";
    if (!ReadMember(in, "face", f.face) || !ReadMember(in, "type", typeId) || !ReadMember(in, "throughAll", f.throughAll) || !ReadMember(in, "diameter", f.diameter) ||
        !ReadMember(in, "depth", f.depth) || !ReadMember(in, "headDiameter", f.headDiameter) ||
        !ReadMember(in, "headDepth", f.headDepth) || !ReadMember(in, "sinkDiameter", f.sinkDiameter))
        return Error{"Отверстие: повреждены параметры"};
    if (!json_io::FromId(kTypeIds, typeId, f.type)) return Error{"Отверстие: неизвестный тип «" + typeId + "»"};
    for (const Json& c : in.value("centres", Json::array())) {
        Vec2 v;
        if (!json_io::Read(c, v)) return Error{"Отверстие: повреждены центры"};
        f.centres.push_back(v);
    }
    // Only the type's own fields: id, name and suppressed are already read.
    face         = f.face;
    centres      = std::move(f.centres);
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
