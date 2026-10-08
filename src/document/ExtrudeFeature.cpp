#include "document/ExtrudeFeature.h"

#include "document/JsonIO.h"

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;
using json_io::ToJson;

bool BooleanOpFromId(std::string_view id, BooleanOp& out) {
    for (BooleanOp op : {BooleanOp::NewBody, BooleanOp::Join, BooleanOp::Cut, BooleanOp::Intersect})
        if (BooleanOpId(op) == id) {
            out = op;
            return true;
        }
    return false;
}

bool ExtentTypeFromId(std::string_view id, ExtentType& out) {
    for (ExtentType e : {ExtentType::Distance, ExtentType::Symmetric, ExtentType::ThroughAll})
        if (ExtentTypeId(e) == id) {
            out = e;
            return true;
        }
    return false;
}

} // namespace

std::string_view BooleanOpId(BooleanOp op) {
    switch (op) {
        case BooleanOp::NewBody:   return "newBody";
        case BooleanOp::Join:      return "join";
        case BooleanOp::Cut:       return "cut";
        case BooleanOp::Intersect: return "intersect";
    }
    return "newBody";
}

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
    Json regions = Json::array();
    for (Vec2 p : profiles.regions) regions.push_back(ToJson(p));
    out["sketch"]    = ToJson(sketch);
    out["profiles"]  = Json{{"automatic", profiles.automatic}, {"regions", std::move(regions)}};
    out["operation"] = BooleanOpId(operation);
    out["extent"]    = ExtentTypeId(extent);
    out["reversed"]  = reversed;
    out["distance"]  = ToJson(distance);
    out["taper"]     = ToJson(taper);
}

Status ExtrudeFeature::LoadFields(const Json& in) {
    ExtrudeFeature f;
    std::string op = "newBody", ext = "distance";
    if (!ReadMember(in, "sketch", f.sketch) || !ReadMember(in, "operation", op) || !ReadMember(in, "extent", ext) ||
        !ReadMember(in, "reversed", f.reversed) || !ReadMember(in, "distance", f.distance) ||
        !ReadMember(in, "taper", f.taper))
        return Error{"Выдавливание: повреждены параметры"};
    if (!BooleanOpFromId(op, f.operation)) return Error{"Выдавливание: неизвестная операция «" + op + "»"};
    if (!ExtentTypeFromId(ext, f.extent)) return Error{"Выдавливание: неизвестный способ «" + ext + "»"};

    if (const auto it = in.find("profiles"); it != in.end()) {
        if (!ReadMember(*it, "automatic", f.profiles.automatic)) return Error{"Выдавливание: повреждён профиль"};
        for (const Json& p : it->value("regions", Json::array())) {
            Vec2 v;
            if (!json_io::Read(p, v)) return Error{"Выдавливание: повреждён профиль"};
            f.profiles.regions.push_back(v);
        }
    }

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
