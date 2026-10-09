#include "document/DraftFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

std::vector<ObjectId> DraftFeature::Dependencies() const {
    if (plane && !origin::IsOriginElement(plane)) return {plane};
    return {};
}

void DraftFeature::SaveFields(Json& out) const {
    Json list = Json::array();
    for (const FaceReference& f : faces) list.push_back(json_io::ToJson(f));
    out["faces"]    = std::move(list);
    out["plane"]    = json_io::ToJson(plane);
    out["reversed"] = reversed;
    out["angle"]    = json_io::ToJson(angle);
}

Status DraftFeature::LoadFields(const Json& in) {
    std::vector<FaceReference> read;
    for (const Json& j : in.value("faces", Json::array())) {
        FaceReference f;
        if (!json_io::Read(j, f)) return Error{"Уклон: повреждены грани"};
        read.push_back(f);
    }
    ObjectId p;
    bool rev = false;
    Parameter a = angle;
    if (!ReadMember(in, "plane", p) || !ReadMember(in, "reversed", rev) || !ReadMember(in, "angle", a))
        return Error{"Уклон: повреждены параметры"};
    faces = std::move(read);
    plane = p;
    reversed = rev;
    angle = std::move(a);
    return Status::Ok();
}

} // namespace bz::doc
