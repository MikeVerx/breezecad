#include "document/ExtrudeFeature.h"

#include "document/JsonIO.h"

#include <format>

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;
using json_io::ToJson;

bool ExtentTypeFromId(std::string_view id, ExtentType& out) {
    for (ExtentType e : {ExtentType::Distance, ExtentType::Symmetric, ExtentType::ThroughAll, ExtentType::ToPlane, ExtentType::Asymmetric})
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
        case ExtentType::ToPlane:    return "toPlane";
        case ExtentType::Asymmetric: return "asymmetric";
    }
    return "distance";
}

void ExtrudeFeature::CollectParameters(std::vector<Parameter*>& out) {
    out.push_back(&distance);
    out.push_back(&taper);
    out.push_back(&distance2);   // last: panels copy parameters by position
}

void ExtrudeFeature::SaveFields(Json& out) const {
    SaveSolidFields(out);
    out["extent"]   = ExtentTypeId(extent);
    out["reversed"] = reversed;
    out["distance"] = ToJson(distance);
    out["taper"]    = ToJson(taper);
    if (extent == ExtentType::Asymmetric) out["distance2"] = ToJson(distance2);   // added in 0.2.2, optional
    if (extent == ExtentType::ToPlane) {
        if (toPlane) out["toPlane"] = toPlane.value;
        if (toFace.IsSet()) out["toFace"] = ToJson(toFace);
    }
}

Status ExtrudeFeature::LoadFields(const Json& in) {
    ExtrudeFeature f;
    if (Status s = f.LoadSolidFields(in, "Выдавливание"); !s) return s;
    std::string ext = "distance";
    if (!ReadMember(in, "extent", ext) || !ReadMember(in, "reversed", f.reversed) || !ReadMember(in, "distance", f.distance) ||
        !ReadMember(in, "taper", f.taper) || !ReadMember(in, "distance2", f.distance2))
        return Error{"Выдавливание: повреждены параметры"};
    if (!ExtentTypeFromId(ext, f.extent)) return Error{std::format("Выдавливание: неизвестный способ «{}»", ext)};
    if (const auto it = in.find("toPlane"); it != in.end() && it->is_number_unsigned()) f.toPlane = ObjectId{it->get<std::uint64_t>()};
    if (const auto it = in.find("toFace"); it != in.end() && !json_io::Read(*it, f.toFace))
        return Error{"Выдавливание: повреждена грань «до»"};

    sketch    = f.sketch;
    profiles  = std::move(f.profiles);
    operation = f.operation;
    extent    = f.extent;
    reversed  = f.reversed;
    distance  = std::move(f.distance);
    taper     = std::move(f.taper);
    distance2 = std::move(f.distance2);
    toPlane   = f.toPlane;
    toFace    = f.toFace;
    return Status::Ok();
}

} // namespace bz::doc
