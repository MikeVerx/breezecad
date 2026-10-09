#include "document/CoilFeature.h"

#include "document/JsonIO.h"

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

void CoilFeature::CollectParameters(std::vector<Parameter*>& out) {
    out.push_back(&pitch);
    out.push_back(&turns);
}

void CoilFeature::SaveFields(Json& out) const {
    SaveSolidFields(out);
    out["axisLine"]   = json_io::ToJson(axisLine);
    out["leftHanded"] = leftHanded;
    out["pitch"]      = json_io::ToJson(pitch);
    out["turns"]      = json_io::ToJson(turns);
}

Status CoilFeature::LoadFields(const Json& in) {
    CoilFeature f;
    if (Status s = f.LoadSolidFields(in, "Пружина"); !s) return s;
    if (!ReadMember(in, "axisLine", f.axisLine) || !ReadMember(in, "leftHanded", f.leftHanded) ||
        !ReadMember(in, "pitch", f.pitch) || !ReadMember(in, "turns", f.turns))
        return Error{"Пружина: повреждены параметры"};
    sketch     = f.sketch;
    profiles   = std::move(f.profiles);
    operation  = f.operation;
    axisLine   = f.axisLine;
    leftHanded = f.leftHanded;
    pitch      = std::move(f.pitch);
    turns      = std::move(f.turns);
    return Status::Ok();
}

} // namespace bz::doc
