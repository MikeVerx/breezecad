#include "document/PatternFeature.h"

#include "document/JsonIO.h"
#include "document/Origin.h"

#include <cmath>
#include <format>

namespace bz::doc {
namespace {

using json_io::FromId;
using json_io::IdOf;
using json_io::Json;
using json_io::ReadMember;

constexpr std::array<std::pair<ModelAxis, std::string_view>, 3> kAxisIds{{
    {ModelAxis::X, "x"}, {ModelAxis::Y, "y"}, {ModelAxis::Z, "z"}}};

constexpr std::array<std::pair<PatternKind, std::string_view>, 2> kKindIds{{
    {PatternKind::Rectangular, "rectangular"}, {PatternKind::Circular, "circular"}}};

constexpr int kMaxCopies = 1000;   // a typo like 3000 should not freeze the app

/// A count parameter as a whole number of instances (at least one).
Expected<int> Count(const Parameter& p) {
    if (p.HasError()) return Error{p.error};
    const double n = std::round(p.value);
    if (n < 1 || std::abs(p.value - n) > 1e-9) return Error{"Количество должно быть целым числом не меньше 1"};
    if (n > kMaxCopies) return Error{std::format("Слишком много копий (больше {})", kMaxCopies)};
    return static_cast<int>(n);
}

bool ReadAxis(const Json& in, const char* key, ModelAxis& out) {
    std::string id = std::string(IdOf(kAxisIds, out));
    return ReadMember(in, key, id) && FromId(kAxisIds, id, out);
}

} // namespace

Vec3 AxisDirection(ModelAxis axis) {
    switch (axis) {
        case ModelAxis::X: return {1, 0, 0};
        case ModelAxis::Y: return {0, 1, 0};
        case ModelAxis::Z: break;
    }
    return {0, 0, 1};
}

// --- CopyFeature ----------------------------------------------------------------

void CopyFeature::SaveSources(Json& out) const {
    Json ids = Json::array();
    for (ObjectId s : sources) ids.push_back(json_io::ToJson(s));
    out["sources"] = std::move(ids);
}

Status CopyFeature::LoadSources(const Json& in, const std::string& what) {
    std::vector<ObjectId> ids;
    for (const Json& j : in.value("sources", Json::array())) {
        ObjectId source;
        if (!json_io::Read(j, source)) return Error{std::format("{}: повреждён список операций", what)};
        ids.push_back(source);
    }
    sources = std::move(ids);
    return Status::Ok();
}

// --- PatternFeature -------------------------------------------------------------

void PatternFeature::CollectParameters(std::vector<Parameter*>& out) {
    for (Parameter* p : {&count1, &spacing1, &count2, &spacing2, &circularCount, &angle}) out.push_back(p);
}

Expected<std::vector<PatternFeature::Copy>> PatternFeature::Copies() const {
    std::vector<Copy> copies;
    if (kind == PatternKind::Circular) {
        const Expected<int> n = Count(circularCount);
        if (!n) return n.GetError();
        if (angle.HasError()) return Error{angle.error};
        // A full turn: n copies around; a part of a turn: first and last at its ends.
        const bool fullTurn = std::abs(std::abs(angle.value) - 2 * kPi) < 1e-9;
        const double step = *n <= 1 ? 0.0 : fullTurn ? angle.value / *n : angle.value / (*n - 1);
        for (int i = 1; i < *n; ++i) copies.push_back(Copy{Vec3{}, step * i});
        return copies;
    }
    const Expected<int> n1 = Count(count1);
    if (!n1) return n1.GetError();
    const Expected<int> n2 = useDirection2 ? Count(count2) : Expected<int>(1);
    if (!n2) return n2.GetError();
    for (const Parameter* p : {&spacing1, &spacing2})
        if (p->HasError()) return Error{p->error};
    const Vec3 step1 = AxisDirection(direction1) * (reversed1 ? -spacing1.value : spacing1.value);
    const Vec3 step2 = AxisDirection(direction2) * (reversed2 ? -spacing2.value : spacing2.value);
    for (int j = 0; j < *n2; ++j)
        for (int i = 0; i < *n1; ++i)
            if (i != 0 || j != 0) copies.push_back(Copy{step1 * i + step2 * j, 0.0});
    return copies;
}

void PatternFeature::SaveFields(Json& out) const {
    SaveSources(out);
    out["kind"]          = IdOf(kKindIds, kind);
    out["direction1"]    = IdOf(kAxisIds, direction1);
    out["reversed1"]     = reversed1;
    out["count1"]        = json_io::ToJson(count1);
    out["spacing1"]      = json_io::ToJson(spacing1);
    out["useDirection2"] = useDirection2;
    out["direction2"]    = IdOf(kAxisIds, direction2);
    out["reversed2"]     = reversed2;
    out["count2"]        = json_io::ToJson(count2);
    out["spacing2"]      = json_io::ToJson(spacing2);
    out["axis"]          = IdOf(kAxisIds, axis);
    out["workAxis"]      = json_io::ToJson(workAxis);
    out["circularCount"] = json_io::ToJson(circularCount);
    out["angle"]         = json_io::ToJson(angle);
}

Status PatternFeature::LoadFields(const Json& in) {
    PatternFeature f;
    std::string kindId = "rectangular";
    if (!ReadMember(in, "kind", kindId) || !FromId(kKindIds, kindId, f.kind) || !ReadAxis(in, "direction1", f.direction1) ||
        !ReadMember(in, "reversed1", f.reversed1) || !ReadMember(in, "count1", f.count1) ||
        !ReadMember(in, "spacing1", f.spacing1) || !ReadMember(in, "useDirection2", f.useDirection2) ||
        !ReadAxis(in, "direction2", f.direction2) || !ReadMember(in, "reversed2", f.reversed2) ||
        !ReadMember(in, "count2", f.count2) || !ReadMember(in, "spacing2", f.spacing2) || !ReadAxis(in, "axis", f.axis) ||
        !ReadMember(in, "circularCount", f.circularCount) || !ReadMember(in, "angle", f.angle) ||
        !ReadMember(in, "workAxis", f.workAxis))
        return Error{"Массив: повреждены параметры"};
    if (Status s = f.LoadSources(in, "Массив"); !s) return s;
    // Only the type's own fields: id, name and suppressed are already read.
    sources       = std::move(f.sources);
    kind          = f.kind;
    direction1    = f.direction1;
    workAxis      = f.workAxis;
    reversed1     = f.reversed1;
    count1        = std::move(f.count1);
    spacing1      = std::move(f.spacing1);
    useDirection2 = f.useDirection2;
    direction2    = f.direction2;
    reversed2     = f.reversed2;
    count2        = std::move(f.count2);
    spacing2      = std::move(f.spacing2);
    axis          = f.axis;
    circularCount = std::move(f.circularCount);
    angle         = std::move(f.angle);
    return Status::Ok();
}

// --- MirrorFeature --------------------------------------------------------------

std::vector<ObjectId> MirrorFeature::Dependencies() const {
    std::vector<ObjectId> deps = sources;
    if (plane && !origin::IsOriginElement(plane)) deps.push_back(plane);   // a work plane
    return deps;
}

void MirrorFeature::SaveFields(Json& out) const {
    SaveSources(out);
    out["plane"] = json_io::ToJson(plane);
    if (wholeBody) out["wholeBody"] = true;
}

Status MirrorFeature::LoadFields(const Json& in) {
    ObjectId p;
    if (!ReadMember(in, "plane", p)) return Error{"Зеркало: повреждены параметры"};
    if (Status s = LoadSources(in, "Зеркало"); !s) return s;
    plane = p;
    wholeBody = in.value("wholeBody", false);
    return Status::Ok();
}

} // namespace bz::doc
