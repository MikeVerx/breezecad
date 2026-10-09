#include "document/CoilFeature.h"

#include "document/JsonIO.h"

namespace bz::doc {
namespace {

using json_io::FromId;
using json_io::IdOf;
using json_io::Json;
using json_io::ReadMember;

constexpr std::array<std::pair<ModelAxis, std::string_view>, 3> kAxisIds{{
    {ModelAxis::X, "x"}, {ModelAxis::Y, "y"}, {ModelAxis::Z, "z"}}};

} // namespace

void CoilFeature::CollectParameters(std::vector<Parameter*>& out) {
    for (Parameter* p : {&radius, &pitch, &turns, &wire, &start}) out.push_back(p);
}

void CoilFeature::SaveFields(Json& out) const {
    out["axis"]       = IdOf(kAxisIds, axis);
    out["operation"]  = BooleanOpId(operation);
    out["leftHanded"] = leftHanded;
    out["radius"]     = json_io::ToJson(radius);
    out["pitch"]      = json_io::ToJson(pitch);
    out["turns"]      = json_io::ToJson(turns);
    out["wire"]       = json_io::ToJson(wire);
    out["start"]      = json_io::ToJson(start);
}

Status CoilFeature::LoadFields(const Json& in) {
    CoilFeature f;
    std::string axisId = "z", op = "newBody";
    if (!ReadMember(in, "axis", axisId) || !FromId(kAxisIds, axisId, f.axis) || !ReadMember(in, "operation", op) ||
        !BooleanOpFromId(op, f.operation) || !ReadMember(in, "leftHanded", f.leftHanded) || !ReadMember(in, "radius", f.radius) ||
        !ReadMember(in, "pitch", f.pitch) || !ReadMember(in, "turns", f.turns) || !ReadMember(in, "wire", f.wire) ||
        !ReadMember(in, "start", f.start))
        return Error{"Пружина: повреждены параметры"};
    axis       = f.axis;
    operation  = f.operation;
    leftHanded = f.leftHanded;
    radius     = std::move(f.radius);
    pitch      = std::move(f.pitch);
    turns      = std::move(f.turns);
    wire       = std::move(f.wire);
    start      = std::move(f.start);
    return Status::Ok();
}

} // namespace bz::doc
