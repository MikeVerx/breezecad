#include "document/WorkPlaneFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

Frame OffsetFrame(const Frame& base, double distance) {
    Frame f = base;
    f.origin = base.origin + base.normal * distance;
    return f;
}

std::vector<ObjectId> WorkPlaneFeature::Dependencies() const {
    if (!face.IsSet() && base && !origin::IsOriginElement(base)) return {base};
    return {};
}

void WorkPlaneFeature::SaveFields(Json& out) const {
    out["base"]     = json_io::ToJson(base);
    if (face.IsSet()) out["face"] = json_io::ToJson(face);
    out["reversed"] = reversed;
    out["offset"]   = json_io::ToJson(offset);
}

Status WorkPlaneFeature::LoadFields(const Json& in) {
    ObjectId b;
    FaceReference fr;
    bool rev = false;
    Parameter off = offset;
    if (!ReadMember(in, "base", b) || !ReadMember(in, "face", fr) || !ReadMember(in, "reversed", rev) ||
        !ReadMember(in, "offset", off))
        return Error{"Рабочая плоскость: повреждены параметры"};
    // Only the type's own fields: id, name and suppressed are already read.
    base = b;
    face = fr;
    reversed = rev;
    offset = std::move(off);
    return Status::Ok();
}

} // namespace bz::doc
