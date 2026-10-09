#include "document/ShellFeature.h"

#include "document/JsonIO.h"

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

void ShellFeature::SaveFields(Json& out) const {
    Json faces = Json::array();
    for (const FaceReference& f : openFaces) faces.push_back(json_io::ToJson(f));
    out["openFaces"] = std::move(faces);
    out["outward"]   = outward;
    out["thickness"] = json_io::ToJson(thickness);
}

Status ShellFeature::LoadFields(const Json& in) {
    std::vector<FaceReference> faces;
    for (const Json& j : in.value("openFaces", Json::array())) {
        FaceReference f;
        if (!json_io::Read(j, f)) return Error{"Оболочка: повреждены грани"};
        faces.push_back(f);
    }
    bool out = false;
    Parameter t = thickness;
    if (!ReadMember(in, "outward", out) || !ReadMember(in, "thickness", t)) return Error{"Оболочка: повреждены параметры"};
    // Only the type's own fields: id, name and suppressed are already read.
    openFaces = std::move(faces);
    outward = out;
    thickness = std::move(t);
    return Status::Ok();
}

} // namespace bz::doc
