#include "document/SheetMetalFeature.h"

#include "document/JsonIO.h"

#include <array>
#include <format>
#include <utility>

namespace bz::doc {
using json_io::Json;
using json_io::ReadMember;
using json_io::ToJson;

std::string SheetNumber(double v, int decimals) {
    std::string s = std::format("{:.{}f}", v, decimals);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

// --- Лист -----------------------------------------------------------------------

void SheetMetalFeature::CollectParameters(std::vector<Parameter*>& out) {
    out.push_back(&thickness);
    out.push_back(&radius);
    out.push_back(&kFactor);
}

sheet::Rules SheetMetalFeature::Rules() const {
    sheet::Rules r;
    r.thickness = thickness.value;
    r.radius = radiusAuto ? sheet::DefaultRadius(r.thickness) : radius.value;
    r.kFactor = kAuto ? sheet::KFactorDin6935(r.radius, r.thickness) : kFactor.value;
    return r;
}

void SheetMetalFeature::ApplyAuto() {
    const sheet::Rules r = Rules();
    if (radiusAuto) {
        radius.value = r.radius;
        radius.expression = SheetNumber(r.radius);
        radius.error.clear();
    }
    if (kAuto) {
        kFactor.value = r.kFactor;
        kFactor.expression = SheetNumber(r.kFactor);
        kFactor.error.clear();
    }
}

void SheetMetalFeature::SaveFields(Json& out) const {
    SaveSolidFields(out);
    out["thickness"]  = ToJson(thickness);
    out["radius"]     = ToJson(radius);
    out["radiusAuto"] = radiusAuto;
    out["kFactor"]    = ToJson(kFactor);
    out["kAuto"]      = kAuto;
    out["reversed"]   = reversed;
}

Status SheetMetalFeature::LoadFields(const Json& in) {
    SheetMetalFeature f;
    if (Status s = f.LoadSolidFields(in, "Лист"); !s) return s;
    if (!ReadMember(in, "thickness", f.thickness) || !ReadMember(in, "radius", f.radius) || !ReadMember(in, "radiusAuto", f.radiusAuto) ||
        !ReadMember(in, "kFactor", f.kFactor) || !ReadMember(in, "kAuto", f.kAuto) || !ReadMember(in, "reversed", f.reversed))
        return Error{"Лист: повреждены параметры"};
    sketch     = f.sketch;
    profiles   = std::move(f.profiles);
    operation  = BooleanOp::NewBody;
    thickness  = std::move(f.thickness);
    radius     = std::move(f.radius);
    radiusAuto = f.radiusAuto;
    kFactor    = std::move(f.kFactor);
    kAuto      = f.kAuto;
    reversed   = f.reversed;
    return Status::Ok();
}

// --- Отбортовка -----------------------------------------------------------------------

void FlangeFeature::CollectParameters(std::vector<Parameter*>& out) {
    out.push_back(&height);
    out.push_back(&angle);
    out.push_back(&offset1);
    out.push_back(&offset2);
    out.push_back(&width);
}

namespace {
constexpr std::array<std::string_view, 3> kEdges{"far", "start", "end"};
constexpr std::array<std::string_view, 3> kWidths{"edge", "offsets", "centered"};
template <class E, std::size_t N>
bool FromId(const std::array<std::string_view, N>& ids, const std::string& id, E& out) {
    for (std::size_t i = 0; i < N; ++i)
        if (ids[i] == id) {
            out = static_cast<E>(i);
            return true;
        }
    return false;
}
} // namespace

void FlangeFeature::SaveFields(Json& out) const {
    out["sheet"]  = ToJson(sheet);
    if (edgeSet) out["edge"] = ToJson(edge);
    if (onFlange) {
        out["onFlange"] = ToJson(onFlange);
        out["onEdge"] = kEdges[static_cast<std::size_t>(onEdge)];
    }
    if (widthMode != FlangeWidth::Edge) {
        out["widthMode"] = kWidths[static_cast<std::size_t>(widthMode)];
        out["offset1"] = ToJson(offset1);
        out["offset2"] = ToJson(offset2);
        out["width"] = ToJson(width);
    }
    out["height"] = ToJson(height);
    out["angle"]  = ToJson(angle);
    out["down"]   = down;
}

Status FlangeFeature::LoadFields(const Json& in) {
    FlangeFeature f;
    if (!ReadMember(in, "sheet", f.sheet) || !ReadMember(in, "height", f.height) || !ReadMember(in, "angle", f.angle) ||
        !ReadMember(in, "down", f.down))
        return Error{"Отбортовка: повреждены параметры"};
    if (!ReadMember(in, "onFlange", f.onFlange)) return Error{"Отбортовка: повреждена ссылка на полку"};
    std::string edgeId = "far", widthId = "edge";
    if (!ReadMember(in, "onEdge", edgeId) || !FromId(kEdges, edgeId, f.onEdge)) return Error{"Отбортовка: неизвестное ребро полки"};
    if (!ReadMember(in, "widthMode", widthId) || !FromId(kWidths, widthId, f.widthMode)) return Error{"Отбортовка: неизвестный способ ширины"};
    if (!ReadMember(in, "offset1", f.offset1) || !ReadMember(in, "offset2", f.offset2) || !ReadMember(in, "width", f.width))
        return Error{"Отбортовка: повреждены размеры ширины"};
    if (const auto it = in.find("edge"); it != in.end()) {
        if (!json_io::Read(*it, f.edge)) return Error{"Отбортовка: повреждено ребро"};
        f.edgeSet = true;
    }
    sheet   = f.sheet;
    onFlange = f.onFlange;
    onEdge  = f.onEdge;
    widthMode = f.widthMode;
    offset1 = std::move(f.offset1);
    offset2 = std::move(f.offset2);
    width   = std::move(f.width);
    edge    = f.edge;
    edgeSet = f.edgeSet;
    height  = std::move(f.height);
    angle   = std::move(f.angle);
    down    = f.down;
    return Status::Ok();
}

// --- Вырез -----------------------------------------------------------------------------

void SheetCutFeature::SaveFields(Json& out) const {
    SaveSolidFields(out);
    out["sheet"] = ToJson(sheet);
}

Status SheetCutFeature::LoadFields(const Json& in) {
    SheetCutFeature f;
    if (Status st = f.LoadSolidFields(in, "Вырез"); !st) return st;
    if (!ReadMember(in, "sheet", f.sheet)) return Error{"Вырез: повреждена ссылка на лист"};
    sketch = f.sketch;
    profiles = std::move(f.profiles);
    operation = BooleanOp::Cut;
    sheet = f.sheet;
    return Status::Ok();
}

} // namespace bz::doc
