#include "document/SectionFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

namespace bz::doc {

std::vector<ObjectId> SectionFeature::Dependencies() const {
    if (plane && !origin::IsOriginElement(plane)) return {plane};
    return {};
}

void SectionFeature::SaveFields(nlohmann::json& out) const {
    out["plane"] = json_io::ToJson(plane);
    out["flipped"] = flipped;
    if (face.IsSet()) out["face"] = json_io::ToJson(face);
}

Status SectionFeature::LoadFields(const nlohmann::json& in) {
    ObjectId p;
    bool f = false;
    FaceReference fr;
    if (!json_io::ReadMember(in, "plane", p) || !json_io::ReadMember(in, "flipped", f) || !json_io::ReadMember(in, "face", fr))
        return Error{"Сечение: повреждены параметры"};
    plane = p;
    flipped = f;
    face = fr;
    return Status::Ok();
}

} // namespace bz::doc
