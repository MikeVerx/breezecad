#include "document/ShellFeature.h"

#include "document/JsonIO.h"

#include <utility>

namespace bz::doc {
using json_io::Json;

void ShellFeature::SaveFields(Json& out) const {
    Json list = Json::array();
    for (const FaceReference& f : faces) list.push_back(json_io::ToJson(f));
    out["faces"] = std::move(list);
    out["thickness"] = json_io::ToJson(thickness);
    out["outward"] = outward;
}

Status ShellFeature::LoadFields(const Json& in) {
    ShellFeature f;
    if (const auto it = in.find("faces"); it != in.end()) {
        if (!it->is_array()) return Error{"Оболочка: повреждён список граней"};
        for (const Json& j : *it) {
            FaceReference r;
            if (!json_io::Read(j, r)) return Error{"Оболочка: повреждено описание грани"};
            f.faces.push_back(r);
        }
    }
    if (!json_io::ReadMember(in, "thickness", f.thickness) || !json_io::ReadMember(in, "outward", f.outward))
        return Error{"Оболочка: повреждены параметры"};
    faces = std::move(f.faces);
    thickness = std::move(f.thickness);
    outward = f.outward;
    return Status::Ok();
}

} // namespace bz::doc
