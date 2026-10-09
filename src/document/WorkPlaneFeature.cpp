#include "document/WorkPlaneFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

#include <array>
#include <format>
#include <string>

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

namespace {

constexpr std::array<std::pair<WorkPlaneMethod, std::string_view>, 4> kMethodIds{{
    {WorkPlaneMethod::Offset, "offset"},
    {WorkPlaneMethod::Midplane, "midplane"},
    {WorkPlaneMethod::Angle, "angle"},
    {WorkPlaneMethod::ThreePoints, "points"},
}};

} // namespace

Frame OffsetFrame(const Frame& base, double distance) {
    Frame f = base;
    f.origin = base.origin + base.normal * distance;
    return f;
}

std::vector<ObjectId> WorkPlaneFeature::Dependencies() const {
    std::vector<ObjectId> out;
    if (!face.IsSet() && base && !origin::IsOriginElement(base)) out.push_back(base);
    if (method == WorkPlaneMethod::Midplane && !face2.IsSet() && base2 && !origin::IsOriginElement(base2)) out.push_back(base2);
    return out;
}

void WorkPlaneFeature::SaveFields(Json& out) const {
    out["base"]     = json_io::ToJson(base);
    if (face.IsSet()) out["face"] = json_io::ToJson(face);
    out["reversed"] = reversed;
    out["offset"]   = json_io::ToJson(offset);
    out["method"]   = std::string(json_io::IdOf(kMethodIds, method));
    if (method == WorkPlaneMethod::Midplane) {
        out["base2"] = json_io::ToJson(base2);
        if (face2.IsSet()) out["face2"] = json_io::ToJson(face2);
    }
    if (method == WorkPlaneMethod::ThreePoints) {
        Json pts = Json::array();
        for (const Vec3& p : points) pts.push_back({p.x, p.y, p.z});
        out["points"] = std::move(pts);
    }
    if (method == WorkPlaneMethod::Angle) {
        if (edge) out["edge"] = json_io::ToJson(*edge);
        out["angle"] = json_io::ToJson(angle);
    }
}

Status WorkPlaneFeature::LoadFields(const Json& in) {
    ObjectId b;
    FaceReference fr;
    bool rev = false;
    Parameter off = offset;
    // Files older than the methods have none of the new members: Offset.
    std::string methodId = "offset";
    ObjectId b2;
    FaceReference fr2;
    Parameter ang = angle;
    std::optional<EdgeReference> e;
    if (const auto it = in.find("edge"); it != in.end()) {
        EdgeReference read;
        if (!json_io::Read(*it, read)) return Error{"Рабочая плоскость: повреждено ребро"};
        e = read;
    }
    if (!ReadMember(in, "base", b) || !ReadMember(in, "face", fr) || !ReadMember(in, "reversed", rev) ||
        !ReadMember(in, "offset", off) || !ReadMember(in, "method", methodId) || !ReadMember(in, "base2", b2) ||
        !ReadMember(in, "face2", fr2) || !ReadMember(in, "angle", ang))
        return Error{"Рабочая плоскость: повреждены параметры"};
    std::vector<Vec3> pts;
    if (const auto it = in.find("points"); it != in.end() && it->is_array())
        for (const Json& p : *it) {
            if (!p.is_array() || p.size() != 3 || !p[0].is_number() || !p[1].is_number() || !p[2].is_number())
                return Error{"Рабочая плоскость: повреждены точки"};
            pts.push_back({p[0].get<double>(), p[1].get<double>(), p[2].get<double>()});
        }
    WorkPlaneMethod m;
    if (!json_io::FromId(kMethodIds, methodId, m)) return Error{std::format("Рабочая плоскость: неизвестный способ «{}»", methodId)};
    // Only the type's own fields: id, name and suppressed are already read.
    base = b;
    face = fr;
    reversed = rev;
    offset = std::move(off);
    method = m;
    base2 = b2;
    face2 = fr2;
    angle = std::move(ang);
    edge = e;
    points = std::move(pts);
    return Status::Ok();
}

} // namespace bz::doc
