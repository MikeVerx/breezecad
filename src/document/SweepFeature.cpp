#include "document/SweepFeature.h"

#include "document/JsonIO.h"

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

std::vector<ObjectId> SweepFeature::Dependencies() const {
    std::vector<ObjectId> deps{sketch};
    if (path) deps.push_back(path);
    return deps;
}

void SweepFeature::SaveFields(Json& out) const {
    SaveSolidFields(out);
    out["path"] = json_io::ToJson(path);
}

Status SweepFeature::LoadFields(const Json& in) {
    SweepFeature f;
    if (Status s = f.LoadSolidFields(in, "Сдвиг"); !s) return s;
    if (!ReadMember(in, "path", f.path)) return Error{"Сдвиг: повреждены параметры"};
    sketch    = f.sketch;
    profiles  = std::move(f.profiles);
    operation = f.operation;
    path      = f.path;
    return Status::Ok();
}

} // namespace bz::doc
