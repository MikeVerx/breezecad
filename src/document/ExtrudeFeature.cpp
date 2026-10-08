#include "document/ExtrudeFeature.h"

#include "document/JsonIO.h"

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;
using json_io::ToJson;

bool ExtentTypeFromId(std::string_view id, ExtentType& out) {
    for (ExtentType e : {ExtentType::Distance, ExtentType::Symmetric, ExtentType::ThroughAll})
        if (ExtentTypeId(e) == id) {
            out = e;
            return true;
        }
    return false;
}

} // namespace

std::string_view ExtentTypeId(ExtentType extent) {
    switch (extent) {
        case ExtentType::Distance:   return "distance";
        case ExtentType::Symmetric:  return "symmetric";
        case ExtentType::ThroughAll: return "throughAll";
    }
    return "distance";
}

void ExtrudeFeature::CollectParameters(std::vector<Parameter*>& out) {
    out.push_back(&distance);
    out.push_back(&taper);
}

void ExtrudeFeature::SaveFields(Json& out) const {
    SaveSolidFields(out);
    out["extent"]   = ExtentTypeId(extent);
    out["reversed"] = reversed;
    out["distance"] = ToJson(distance);
    out["taper"]    = ToJson(taper);
}

Status ExtrudeFeature::LoadFields(const Json& in) {
    ExtrudeFeature f;
    if (Status s = f.LoadSolidFields(in, "Выдавливание"); !s) return s;
    std::string ext = "distance";
    if (!ReadMember(in, "extent", ext) || !ReadMember(in, "reversed", f.reversed) || !ReadMember(in, "distance", f.distance) ||
        !ReadMember(in, "taper", f.taper))
        return Error{"Выдавливание: повреждены параметры"};
    if (!ExtentTypeFromId(ext, f.extent)) return Error{"Выдавливание: неизвестный способ «" + ext + "»"};

    sketch    = f.sketch;
    profiles  = std::move(f.profiles);
    operation = f.operation;
    extent    = f.extent;
    reversed  = f.reversed;
    distance  = std::move(f.distance);
    taper     = std::move(f.taper);
    return Status::Ok();
}

} // namespace bz::doc
