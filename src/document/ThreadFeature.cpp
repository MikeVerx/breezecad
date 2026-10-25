#include "document/ThreadFeature.h"

#include "document/JsonIO.h"

#include <array>
#include <cmath>
#include <utility>

namespace bz::doc {

using json_io::Json;
using json_io::ReadMember;

namespace {

// ISO 261 coarse series: nominal diameter, pitch.
constexpr std::array<std::pair<double, double>, 22> kSeries{{
    {1.6, 0.35}, {2, 0.4},   {2.5, 0.45}, {3, 0.5},   {3.5, 0.6}, {4, 0.7},  {5, 0.8},   {6, 1.0},
    {8, 1.25},   {10, 1.5},  {12, 1.75},  {14, 2.0},  {16, 2.0},  {18, 2.5}, {20, 2.5},  {22, 2.5},
    {24, 3.0},   {27, 3.0},  {30, 3.5},   {36, 4.0},  {42, 4.5},  {48, 5.0},
}};

const std::pair<double, double>& Nearest(double d) {
    const auto* best = &kSeries[0];
    for (const auto& size : kSeries)
        if (std::abs(size.first - d) < std::abs(best->first - d)) best = &size;
    return *best;
}

} // namespace

double CoarsePitch(double d) { return Nearest(d).second; }

std::vector<double> StandardSizes() {
    std::vector<double> out;
    for (const auto& size : kSeries) out.push_back(size.first);
    return out;
}

double StandardSize(double d) {
    const double nominal = Nearest(d).first;
    return std::abs(nominal - d) <= 0.05 * nominal ? nominal : d;
}

void ThreadFeature::SaveFields(Json& out) const {
    if (face)
        out["face"] = {{"point", json_io::ToJson(face->axisPoint)}, {"axis", json_io::ToJson(face->axisDir)},
                       {"radius", face->radius}};
    out["pitch"] = json_io::ToJson(pitch);
    out["fullLength"] = fullLength;
    out["length"] = json_io::ToJson(length);
    out["fromOtherEnd"] = fromOtherEnd;
    out["leftHanded"] = leftHanded;
    if (size > 0) out["size"] = size;
    if (cosmetic) out["cosmetic"] = true;
}

Status ThreadFeature::LoadFields(const Json& in) {
    Parameter p = pitch, l = length;
    bool full = true, other = false, left = false;
    if (!ReadMember(in, "pitch", p) || !ReadMember(in, "fullLength", full) || !ReadMember(in, "length", l) ||
        !ReadMember(in, "fromOtherEnd", other) || !ReadMember(in, "leftHanded", left))
        return Error{"Резьба: повреждены параметры"};
    std::optional<CylinderTouch> f;
    if (const auto it = in.find("face"); it != in.end()) {
        CylinderTouch t;
        if (!it->is_object() || !json_io::Read(it->value("point", Json()), t.axisPoint) ||
            !json_io::Read(it->value("axis", Json()), t.axisDir) || !it->contains("radius") || !(*it)["radius"].is_number())
            return Error{"Резьба: повреждена грань"};
        t.radius = (*it)["radius"].get<double>();
        f = t;
    }
    face = f;
    pitch = std::move(p);
    length = std::move(l);
    fullLength = full;
    fromOtherEnd = other;
    leftHanded = left;
    size = in.value("size", 0.0);
    cosmetic = in.value("cosmetic", false);
    return Status::Ok();
}

} // namespace bz::doc
