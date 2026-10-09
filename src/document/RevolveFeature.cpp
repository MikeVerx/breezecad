#include "document/RevolveFeature.h"

#include "document/JsonIO.h"

#include <array>
#include <format>
#include <utility>

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::FromId;
using json_io::IdOf;
using json_io::ReadMember;

constexpr std::array<std::pair<RevolveAxisKind, std::string_view>, 3> kAxisIds{{
    {RevolveAxisKind::SketchLine, "sketchLine"},
    {RevolveAxisKind::SketchX, "sketchX"},
    {RevolveAxisKind::SketchY, "sketchY"},
}};

constexpr std::array<std::pair<RevolveExtent, std::string_view>, 3> kExtentIds{{
    {RevolveExtent::Full, "full"},
    {RevolveExtent::Angle, "angle"},
    {RevolveExtent::Symmetric, "symmetric"},
}};

} // namespace

void RevolveFeature::CollectParameters(std::vector<Parameter*>& out) { out.push_back(&angle); }

void RevolveFeature::SaveFields(Json& out) const {
    SaveSolidFields(out);
    out["axis"]     = Json{{"kind", IdOf(kAxisIds, axis.kind)}, {"line", json_io::ToJson(axis.line)}};
    out["extent"]   = IdOf(kExtentIds, extent);
    out["reversed"] = reversed;
    out["angle"]    = json_io::ToJson(angle);
}

Status RevolveFeature::LoadFields(const Json& in) {
    RevolveFeature f;
    if (Status s = f.LoadSolidFields(in, "Вращение"); !s) return s;
    std::string kind, ext;
    const auto axisIt = in.find("axis");
    if (axisIt == in.end() || !ReadMember(*axisIt, "kind", kind) || !ReadMember(*axisIt, "line", f.axis.line) ||
        !ReadMember(in, "extent", ext) || !ReadMember(in, "reversed", f.reversed) || !ReadMember(in, "angle", f.angle))
        return Error{"Вращение: повреждены параметры"};
    if (!FromId(kAxisIds, kind, f.axis.kind)) return Error{std::format("Вращение: неизвестная ось «{}»", kind)};
    if (!FromId(kExtentIds, ext, f.extent)) return Error{std::format("Вращение: неизвестный способ «{}»", ext)};

    sketch    = f.sketch;
    profiles  = std::move(f.profiles);
    operation = f.operation;
    axis      = f.axis;
    extent    = f.extent;
    reversed  = f.reversed;
    angle     = std::move(f.angle);
    return Status::Ok();
}

} // namespace bz::doc
