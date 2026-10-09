#include "document/SketchSolidFeature.h"

#include "document/JsonIO.h"

#include <format>

namespace bz::doc {
namespace {

using json_io::Json;

} // namespace

bool BooleanOpFromId(std::string_view id, BooleanOp& out) {
    for (BooleanOp op : {BooleanOp::NewBody, BooleanOp::Join, BooleanOp::Cut, BooleanOp::Intersect})
        if (BooleanOpId(op) == id) {
            out = op;
            return true;
        }
    return false;
}

std::string_view BooleanOpId(BooleanOp op) {
    switch (op) {
        case BooleanOp::NewBody:   return "newBody";
        case BooleanOp::Join:      return "join";
        case BooleanOp::Cut:       return "cut";
        case BooleanOp::Intersect: return "intersect";
    }
    return "newBody";
}

void SketchSolidFeature::SaveSolidFields(Json& out) const {
    Json regions = Json::array();
    for (Vec2 p : profiles.regions) regions.push_back(json_io::ToJson(p));
    out["sketch"]    = json_io::ToJson(sketch);
    out["profiles"]  = Json{{"automatic", profiles.automatic}, {"regions", std::move(regions)}};
    out["operation"] = BooleanOpId(operation);
}

Status SketchSolidFeature::LoadSolidFields(const Json& in, const std::string& what) {
    ObjectId readSketch;
    std::string op = "newBody";
    if (!json_io::ReadMember(in, "sketch", readSketch) || !json_io::ReadMember(in, "operation", op))
        return Error{std::format("{}: повреждены параметры", what)};
    BooleanOp readOp = BooleanOp::NewBody;
    if (!BooleanOpFromId(op, readOp)) return Error{std::format("{}: неизвестная операция «{}»", what, op)};

    ProfileSelection readProfiles;
    if (const auto it = in.find("profiles"); it != in.end()) {
        if (!json_io::ReadMember(*it, "automatic", readProfiles.automatic)) return Error{std::format("{}: повреждён профиль", what)};
        for (const Json& p : it->value("regions", Json::array())) {
            Vec2 v;
            if (!json_io::Read(p, v)) return Error{std::format("{}: повреждён профиль", what)};
            readProfiles.regions.push_back(v);
        }
    }
    sketch = readSketch;
    profiles = std::move(readProfiles);
    operation = readOp;
    return Status::Ok();
}

} // namespace bz::doc
