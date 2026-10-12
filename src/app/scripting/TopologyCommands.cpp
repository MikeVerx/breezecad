// The bodies' faces and edges as numbered lists (kernel/Topology.h): what
// commands that work on geometry (hole, fillet, shell, a sketch on a face…)
// take instead of a click. Numbers hold until the part changes.

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/scripting/Commands.h"
#include "document/JsonIO.h"

#include <cmath>
#include <format>

namespace bz::app::scripting {

namespace {

const char* SurfaceName(doc::SurfaceKind k) {
    switch (k) {
        case doc::SurfaceKind::Plane: return "plane";
        case doc::SurfaceKind::Cylinder: return "cylinder";
        case doc::SurfaceKind::Cone: return "cone";
        case doc::SurfaceKind::Sphere: return "sphere";
        case doc::SurfaceKind::Torus: return "torus";
        default: return "other";
    }
}

const char* CurveName(doc::CurveKind k) {
    switch (k) {
        case doc::CurveKind::Line: return "line";
        case doc::CurveKind::Circle: return "circle";
        default: return "other";
    }
}

double Round(double x) { return std::abs(x) < 5e-7 ? 0.0 : std::round(x * 1e4) / 1e4; }

Json FaceJson(const kernel::FaceInfo& f, const doc::Document& d) {
    Json j = {{"face", f.index},
              {"body", f.body.value},
              {"kind", SurfaceName(f.kind)},
              {"area_mm2", Round(f.area)},
              {"point", ToJson3(f.point)},
              {"normal", ToJson3(f.normal)},
              {"box", {{"min", ToJson3(f.boxMin)}, {"max", ToJson3(f.boxMax)}}},
              {"edges", f.edges}};
    if (f.axis) {
        j["axis_point"] = ToJson3(f.axis->axisPoint);
        j["axis_dir"]   = ToJson3(f.axis->axisDir);
        j["radius"]     = Round(f.axis->radius);
        if (f.kind == doc::SurfaceKind::Cylinder) j["hole"] = f.hole;
    }
    if (const doc::Feature* by = d.Find(f.body)) j["body_name"] = by->name;
    j["ref"] = doc::json_io::ToJson(f.ref);   // as features keep it (part.create_feature)
    return j;
}

Json EdgeJson(const kernel::EdgeInfo& e) {
    Json j = {{"edge", e.index},
              {"body", e.body.value},
              {"kind", CurveName(e.kind)},
              {"length", Round(e.length)},
              {"start", ToJson3(e.start)},
              {"end", ToJson3(e.end)},
              {"middle", ToJson3(e.middle)},
              {"faces", e.faces}};
    if (e.kind == doc::CurveKind::Line) j["direction"] = ToJson3(e.direction);
    if (e.centre) {
        j["centre"] = ToJson3(*e.centre);
        j["axis"]   = ToJson3(e.direction);
        j["radius"] = Round(e.radius);
        j["closed"] = e.closed;
    }
    j["ref"] = doc::json_io::ToJson(e.ref);   // as features keep it (part.create_feature)
    return j;
}

/// The direction filter: parallel to `dir` (either way) within about a degree.
bool Parallel(Vec3 a, Vec3 dir) { return a.Normalized().Cross(dir.Normalized()).Length() < 0.02; }

/// The `limit` entries nearest `near` (when given), else the first `limit`.
template <class T>
std::vector<const T*> Pick(std::vector<const T*> list, const Json& args, Vec3 (*where)(const T&)) {
    if (Has(args, "near")) {
        const Vec3 p = Point3(args["near"], "near");
        std::stable_sort(list.begin(), list.end(), [&](const T* a, const T* b) { return (where(*a) - p).Length() < (where(*b) - p).Length(); });
    }
    const int limit = Integer(args, "limit", 60);
    if (limit > 0 && static_cast<int>(list.size()) > limit) list.resize(static_cast<std::size_t>(limit));
    return list;
}

Json Faces(AppContext& app, const Json& args) {
    Session&                s = ActivePart(app);
    const kernel::Topology& t = TopologyOf(s);
    std::vector<const kernel::FaceInfo*> list;
    const std::string kind = Text(args, "kind", "");
    for (const kernel::FaceInfo& f : t.faces) {
        if (!kind.empty() && kind != SurfaceName(f.kind)) continue;
        if (Has(args, "normal") && (f.kind != doc::SurfaceKind::Plane || f.normal.Dot(Point3(args["normal"], "normal").Normalized()) < 0.9998)) continue;
        if (Has(args, "axis") && (!f.axis || !Parallel(f.axis->axisDir, Point3(args["axis"], "axis")))) continue;
        if (Has(args, "radius") && (!f.axis || std::abs(f.axis->radius - Number(args, "radius")) > 1e-3)) continue;
        if (Has(args, "body") && f.body.value != static_cast<std::uint64_t>(Integer(args, "body"))) continue;
        if (Has(args, "holes_only") && Flag(args, "holes_only", false) && !f.hole) continue;
        list.push_back(&f);
    }
    const std::size_t found = list.size();
    list = Pick<kernel::FaceInfo>(list, args, [](const kernel::FaceInfo& f) { return f.point; });
    Json out = Json::array();
    for (const auto* f : list) out.push_back(FaceJson(*f, s.doc));
    return {{"total_faces", t.faces.size()}, {"matching", found}, {"faces", std::move(out)},
            {"note", "Номера действуют до следующего изменения детали."}};
}

Json Edges(AppContext& app, const Json& args) {
    Session&                s = ActivePart(app);
    const kernel::Topology& t = TopologyOf(s);
    std::vector<const kernel::EdgeInfo*> list;
    const std::string kind = Text(args, "kind", "");
    std::optional<int> face;
    if (Has(args, "face")) face = FaceArg(s, args["face"], "face").index;
    for (const kernel::EdgeInfo& e : t.edges) {
        if (!kind.empty() && kind != CurveName(e.kind)) continue;
        if (face && std::find(e.faces.begin(), e.faces.end(), *face) == e.faces.end()) continue;
        if (Has(args, "direction") && (e.kind != doc::CurveKind::Line || !Parallel(e.direction, Point3(args["direction"], "direction")))) continue;
        if (Has(args, "radius") && (!e.centre || std::abs(e.radius - Number(args, "radius")) > 1e-3)) continue;
        if (Has(args, "length") && std::abs(e.length - Number(args, "length")) > 1e-3) continue;
        if (Has(args, "body") && e.body.value != static_cast<std::uint64_t>(Integer(args, "body"))) continue;
        list.push_back(&e);
    }
    const std::size_t found = list.size();
    list = Pick<kernel::EdgeInfo>(list, args, [](const kernel::EdgeInfo& e) { return e.middle; });
    Json out = Json::array();
    for (const auto* e : list) out.push_back(EdgeJson(*e));
    return {{"total_edges", t.edges.size()}, {"matching", found}, {"edges", std::move(out)},
            {"note", "Номера действуют до следующего изменения детали."}};
}

Json Measure(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    TopologyOf(s);
    const auto entity = [&](const char* key) -> std::pair<bool, int> {
        const Json& v = Required(args, key);
        if (!v.is_object() || (!v.contains("face") && !v.contains("edge"))) Fail(std::format("«{}» — {{\"face\": N}} или {{\"edge\": N}}", key));
        const bool isFace = v.contains("face");
        return {isFace, Integer(v, isFace ? "face" : "edge")};
    };
    const auto [aFace, a] = entity("a");
    const auto [bFace, b] = entity("b");
    const auto m = kernel::MeasureBetween(s.model, aFace, a, bFace, b);
    if (!m) Fail("Нет таких граней или рёбер (номера — из part.faces / part.edges)");
    Json out = {{"distance_mm", Round(m->distance)}, {"from", ToJson3(m->from)}, {"to", ToJson3(m->to)}};
    if (m->angle) out["angle_deg"] = Round(*m->angle * 180.0 / kPi);
    return out;
}

Json FilterSchema(bool faces) {
    Json p = {{"kind", EnumArg(faces ? "Тип поверхности." : "Тип кривой.",
                               faces ? std::vector<std::string>{"plane", "cylinder", "cone", "sphere", "torus", "other"}
                                     : std::vector<std::string>{"line", "circle", "other"})},
              {"radius", NumberArg("Только с этим радиусом, мм (цилиндры, окружности).")},
              {"body", IdArg("Только тело с этим id (part.tree → bodies).")},
              {"near", Point3Arg("Сначала ближайшие к этой точке.")},
              {"limit", IntegerArg("Сколько вернуть (по умолчанию 60; 0 — все).")}};
    if (faces) {
        p["normal"]     = Point3Arg("Только плоские грани с этой внешней нормалью (например [0,0,1] — смотрят вверх).");
        p["axis"]       = Point3Arg("Только цилиндры/конусы с осью вдоль этого направления.");
        p["holes_only"] = BoolArg("Только отверстия (цилиндры, у которых материал снаружи).");
    } else {
        p["face"]      = IntegerArg("Только рёбра этой грани (номер из part.faces).");
        p["direction"] = Point3Arg("Только прямые рёбра вдоль этого направления.");
        p["length"]    = NumberArg("Только с этой длиной, мм.");
    }
    return Schema(std::move(p));
}

} // namespace

void AddTopologyCommands(Commands& out) {
    out.push_back({"part.faces",
                   "Грани тел детали пронумерованы: тип (plane/cylinder…), точка на грани, внешняя нормаль, площадь, габарит; "
                   "у цилиндров — ось, радиус и hole (отверстие или вал); рёбра грани. Номера нужны командам, работающим с "
                   "гранями (отверстие, оболочка, эскиз на грани, рабочие плоскости). Фильтры сужают список.",
                   FilterSchema(true), false, &Faces});
    out.push_back({"part.edges",
                   "Рёбра тел детали пронумерованы: тип (line/circle), длина, начало/конец/середина, направление; у окружностей — "
                   "центр, ось, радиус; две грани, которые ребро разделяет. Номера нужны сопряжению, фаске, рабочим осям. "
                   "Пример: все рёбра верхней грани — сначала part.faces {normal:[0,0,1]}, затем part.edges {face: N}.",
                   FilterSchema(false), false, &Edges});
    out.push_back({"part.measure",
                   "Кратчайшее расстояние между двумя гранями/рёбрами (и угол между плоскими гранями или прямыми рёбрами).",
                   Schema({{"a", {{"type", "object"}, {"description", "{\"face\": N} или {\"edge\": N}."}}},
                           {"b", {{"type", "object"}, {"description", "{\"face\": N} или {\"edge\": N}."}}}},
                          {"a", "b"}),
                   false, &Measure});
}

} // namespace bz::app::scripting
