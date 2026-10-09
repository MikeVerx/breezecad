#include "document/LoftFeature.h"

#include "document/JsonIO.h"

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

void LoftFeature::SaveFields(Json& out) const {
    Json ids = Json::array();
    for (ObjectId s : sections) ids.push_back(json_io::ToJson(s));
    out["sections"]  = std::move(ids);
    out["operation"] = BooleanOpId(operation);
    out["ruled"]     = ruled;
}

Status LoftFeature::LoadFields(const Json& in) {
    std::vector<ObjectId> ids;
    for (const Json& j : in.value("sections", Json::array())) {
        ObjectId section;
        if (!json_io::Read(j, section)) return Error{"Лофт: повреждён список сечений"};
        ids.push_back(section);
    }
    std::string op = std::string(BooleanOpId(operation));
    bool r = false;
    BooleanOp parsed = BooleanOp::NewBody;
    if (!ReadMember(in, "operation", op) || !BooleanOpFromId(op, parsed) || !ReadMember(in, "ruled", r))
        return Error{"Лофт: повреждены параметры"};
    sections = std::move(ids);
    operation = parsed;
    ruled = r;
    return Status::Ok();
}

} // namespace bz::doc
