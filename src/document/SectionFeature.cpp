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
}

Status SectionFeature::LoadFields(const nlohmann::json& in) {
    ObjectId p;
    bool f = false;
    if (!json_io::ReadMember(in, "plane", p) || !json_io::ReadMember(in, "flipped", f))
        return Error{"Сечение: повреждены параметры"};
    plane = p;
    flipped = f;
    return Status::Ok();
}

} // namespace bz::doc
