// Work geometry: planes, axes and points to build on — what the
// «Рабочая плоскость / ось / точка» panels make. Faces and edges are
// numbers from part.faces / part.edges; planes are XY / XZ / YZ or a work
// plane (name or id).

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/scripting/Commands.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/WorkPointFeature.h"
#include "kernel/Topology.h"

#include <cmath>
#include <format>

namespace bz::app::scripting {

namespace {

/// A picked point: a work point {"work_point": id} or [x, y, z]. A point
/// that is a vertex of the body is tied to an edge ending there, so it
/// follows the body when the part changes (as a click on a vertex does).
struct Pick {
    Vec3                              at;
    std::optional<doc::EdgeReference> edge;
    ObjectId                          workPoint;
};

std::optional<doc::EdgeReference> VertexEdge(Session& s, Vec3 p) {
    for (const kernel::EdgeInfo& e : TopologyOf(s).edges)
        if ((e.start - p).Length() < 1e-4 || (e.end - p).Length() < 1e-4) return e.ref;
    return std::nullopt;
}

Pick PickArg(Session& s, const Json& v, const char* what) {
    Pick out;
    if (v.is_object() && v.contains("work_point")) {
        out.workPoint = ObjectId{v["work_point"].get<std::uint64_t>()};
        const auto* wp = s.doc.FindAs<doc::WorkPointFeature>(out.workPoint);
        if (!wp || !wp->position) Fail(std::format("«{}»: {} — не построенная рабочая точка", what, out.workPoint.value));
        out.at = *wp->position;
        return out;
    }
    out.at   = Point3(v, what);
    out.edge = VertexEdge(s, out.at);
    return out;
}

void Picks(Session& s, const Json& args, std::size_t count, std::vector<Vec3>& points,
           std::vector<std::optional<doc::EdgeReference>>& edges, std::vector<ObjectId>& workPoints) {
    const Json& list = Required(args, "points");
    if (!list.is_array() || list.size() != count)
        Fail(std::format("«points» — {} точки: [x, y, z] (вершина тела или любая точка) или {{\"work_point\": id}}", count));
    for (const Json& v : list) {
        const Pick p = PickArg(s, v, "points");
        points.push_back(p.at);
        edges.push_back(p.edge);
        workPoints.push_back(p.workPoint);
    }
}

/// The start of a work plane: "plane" (XY…, a work plane) or a flat "face".
void Start(Session& s, const Json& args, const char* planeKey, const char* faceKey, ObjectId& base, doc::FaceReference& face) {
    if (Has(args, faceKey)) {
        const kernel::FaceInfo& f = FaceArg(s, args[faceKey], faceKey);
        if (f.kind != doc::SurfaceKind::Plane) Fail(std::format("«{}» — нужна плоская грань", faceKey));
        face = f.ref;
    } else if (Has(args, planeKey)) {
        base = PlaneArg(s, args, planeKey);
    } else {
        Fail(std::format("Задайте «{}» (XY, XZ, YZ, рабочая плоскость) или «{}» (плоская грань)", planeKey, faceKey));
    }
}

/// A cylinder (or cone) face: its axis, and where a tangent plane touches
/// it — toward `toward` when given, else at the face's own point.
doc::CylinderTouch CylinderArg(Session& s, const Json& args, const char* key) {
    const kernel::FaceInfo& f = FaceArg(s, Required(args, key), key);
    if (!f.axis || (f.kind != doc::SurfaceKind::Cylinder && f.kind != doc::SurfaceKind::Cone))
        Fail(std::format("«{}» — нужна цилиндрическая грань", key));
    doc::CylinderTouch t = *f.axis;
    if (Has(args, "toward")) {
        const Vec3 at = Point3(args["toward"], "toward") - t.axisPoint;
        const Vec3 d  = t.axisDir.Normalized();
        const Vec3 r  = at - d * at.Dot(d);
        if (r.Length() < 1e-9) Fail("«toward» лежит на оси — направление касания не определить");
        t.radial = r.Normalized();
    }
    return t;
}

Json Frame3(const Frame& f) {
    return {{"origin", ToJson3(f.origin)}, {"normal", ToJson3(f.normal)}, {"x_dir", ToJson3(f.xDir)}};
}

Json WorkPlane(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::WorkPlaneFeature>();
    static const std::vector<std::string> kMethods = {"offset", "midplane", "angle", "three_points", "tangent"};
    f->method   = static_cast<doc::WorkPlaneMethod>(Choice(args, "method", kMethods, 0));
    f->reversed = Flag(args, "reversed", false);
    std::vector<Parameter*> name;
    switch (f->method) {
        case doc::WorkPlaneMethod::Offset:
            Start(s, args, "plane", "face", f->base, f->face);
            f->offset.expression = Expression(args, "offset", "0");
            name.push_back(&f->offset);
            break;
        case doc::WorkPlaneMethod::Midplane:
            Start(s, args, "plane", "face", f->base, f->face);
            Start(s, args, "plane2", "face2", f->base2, f->face2);
            break;
        case doc::WorkPlaneMethod::Angle: {
            Start(s, args, "plane", "face", f->base, f->face);
            const kernel::EdgeInfo& e = EdgeArg(s, Required(args, "edge"), "edge");
            if (e.kind != doc::CurveKind::Line) Fail("«edge» — нужно прямое ребро (ось поворота)");
            f->edge = e.ref;
            f->angle.expression = Expression(args, "angle");
            name.push_back(&f->angle);
            break;
        }
        case doc::WorkPlaneMethod::ThreePoints: Picks(s, args, 3, f->points, f->pointEdges, f->pointFeatures); break;
        case doc::WorkPlaneMethod::Tangent: f->cylinder = CylinderArg(s, args, "cylinder"); break;
    }
    Json out = AddToPart(s, std::move(f), args, "Плоскость", name);
    if (const auto* made = s.doc.FindAs<doc::WorkPlaneFeature>(ObjectId{out["id"].get<std::uint64_t>()}); made && made->frame)
        out["frame"] = Frame3(*made->frame);
    return out;
}

Json WorkAxis(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::WorkAxisFeature>();
    static const std::vector<std::string> kMethods = {"edge", "cylinder", "planes", "points"};
    f->method = static_cast<doc::WorkAxisMethod>(Choice(args, "method", kMethods, Has(args, "edge") ? 0 : 1));
    switch (f->method) {
        case doc::WorkAxisMethod::Edge: {
            const kernel::EdgeInfo& e = EdgeArg(s, Required(args, "edge"), "edge");
            if (e.kind != doc::CurveKind::Line) Fail("«edge» — нужно прямое ребро");
            f->edge = e.ref;
            break;
        }
        case doc::WorkAxisMethod::Cylinder: f->cylinder = CylinderArg(s, args, "cylinder"); break;
        case doc::WorkAxisMethod::Planes:
            f->plane1 = PlaneArg(s, args, "plane1");
            f->plane2 = PlaneArg(s, args, "plane2");
            break;
        case doc::WorkAxisMethod::Points: Picks(s, args, 2, f->points, f->pointEdges, f->pointFeatures); break;
    }
    Json out = AddToPart(s, std::move(f), args, "Ось");
    if (const auto* made = s.doc.FindAs<doc::WorkAxisFeature>(ObjectId{out["id"].get<std::uint64_t>()}); made && made->line)
        out["line"] = {{"point", ToJson3(made->line->first)}, {"direction", ToJson3(made->line->second)}};
    return out;
}

Json WorkPoint(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::WorkPointFeature>();
    static const std::vector<std::string> kMethods = {"vertex", "center", "midpoint", "intersection", "coordinates"};
    f->method = static_cast<doc::WorkPointMethod>(Choice(args, "method", kMethods, 4));
    std::vector<Parameter*> name;
    switch (f->method) {
        case doc::WorkPointMethod::Vertex: {
            f->vertex = Point3(Required(args, "point"), "point");
            f->edge   = VertexEdge(s, f->vertex);
            if (!f->edge) Fail("«point» — не вершина тела (вершины — концы рёбер из part.edges)");
            break;
        }
        case doc::WorkPointMethod::Center:
            if (Has(args, "sphere")) {
                const kernel::FaceInfo& face = FaceArg(s, args["sphere"], "sphere");
                if (face.kind != doc::SurfaceKind::Sphere) Fail("«sphere» — нужна сферическая грань");
                f->sphere = face.ref;
            } else {
                const kernel::EdgeInfo& e = EdgeArg(s, Required(args, "edge"), "edge");
                if (!e.centre) Fail("«edge» — нужно круглое ребро (окружность, дуга)");
                f->edge = e.ref;
            }
            break;
        case doc::WorkPointMethod::Midpoint: f->edge = EdgeArg(s, Required(args, "edge"), "edge").ref; break;
        case doc::WorkPointMethod::Intersection:
            if (Has(args, "axis")) {
                f->axis = Id(args, "axis");
                if (!s.doc.FindAs<doc::WorkAxisFeature>(f->axis)) Fail("«axis» — не рабочая ось");
            } else {
                const kernel::EdgeInfo& e = EdgeArg(s, Required(args, "edge"), "edge");
                if (e.kind != doc::CurveKind::Line) Fail("«edge» — нужно прямое ребро или рабочая ось (axis)");
                f->edge = e.ref;
            }
            f->plane = PlaneArg(s, args, "plane");
            break;
        case doc::WorkPointMethod::Coordinates:
            f->x.expression = Expression(args, "x", "0");
            f->y.expression = Expression(args, "y", "0");
            f->z.expression = Expression(args, "z", "0");
            name = {&f->x, &f->y, &f->z};
            break;
    }
    Json out = AddToPart(s, std::move(f), args, "Точка", name);
    if (const auto* made = s.doc.FindAs<doc::WorkPointFeature>(ObjectId{out["id"].get<std::uint64_t>()}); made && made->position)
        out["position"] = ToJson3(*made->position);
    return out;
}

Json Own(Json p) {
    p["name"] = StringArg("Имя в дереве.");
    return p;
}

} // namespace

void AddWorkCommands(Commands& out) {
    out.push_back({"work.plane",
                   "Рабочая плоскость (для эскизов, зеркала, сечения, лофта). method: offset — параллельно plane/face на offset "
                   "(по умолчанию); midplane — посередине между plane/face и plane2/face2; angle — через прямое ребро edge под "
                   "углом angle к plane/face; three_points — через три точки points; tangent — касательно цилиндрической грани "
                   "cylinder (со стороны точки toward). Возвращает id и frame (origin, normal) — id/имя годятся в sketch.create plane.",
                   Schema(Own({{"method", EnumArg("Способ.", {"offset", "midplane", "angle", "three_points", "tangent"})},
                               {"plane", StringArg("XY, XZ, YZ или рабочая плоскость (имя/id) — от чего строить.")},
                               {"face", IntegerArg("Или плоская грань (part.faces).")},
                               {"offset", ExpressionArg("offset: расстояние, мм.")},
                               {"plane2", StringArg("midplane: вторая плоскость.")},
                               {"face2", IntegerArg("midplane: или вторая грань.")},
                               {"edge", IntegerArg("angle: прямое ребро — ось поворота.")},
                               {"angle", ExpressionArg("angle: угол, градусы.")},
                               {"points", {{"type", "array"}, {"description", "three_points: три точки — [x, y, z] (вершина тела следует за ним) или {\"work_point\": id}."}}},
                               {"cylinder", IntegerArg("tangent: цилиндрическая грань.")},
                               {"toward", Point3Arg("tangent: с какой стороны касаться (точка в стороне от оси).")},
                               {"reversed", BoolArg("В другую сторону (смещение, угол, нормаль).")}})),
                   true, &WorkPlane});
    out.push_back({"work.axis",
                   "Рабочая ось (для кругового массива, точек пересечения). method: edge — по прямому ребру; cylinder — ось "
                   "цилиндрической грани (по умолчанию); planes — пересечение plane1 и plane2; points — через две точки. "
                   "Возвращает id и line (point, direction).",
                   Schema(Own({{"method", EnumArg("Способ.", {"edge", "cylinder", "planes", "points"})},
                               {"edge", IntegerArg("edge: прямое ребро.")},
                               {"cylinder", IntegerArg("cylinder: цилиндрическая грань.")},
                               {"plane1", StringArg("planes: первая плоскость.")},
                               {"plane2", StringArg("planes: вторая плоскость.")},
                               {"points", {{"type", "array"}, {"description", "points: две точки — [x, y, z] или {\"work_point\": id}."}}}})),
                   true, &WorkAxis});
    out.push_back({"work.point",
                   "Рабочая точка. method: coordinates — x, y, z (по умолчанию, параметры); vertex — вершина тела point; "
                   "center — центр круглого ребра edge или сферы sphere; midpoint — середина ребра edge; intersection — "
                   "прямое ребро edge или рабочая ось axis пересекает плоскость plane. Возвращает id и position.",
                   Schema(Own({{"method", EnumArg("Способ.", {"vertex", "center", "midpoint", "intersection", "coordinates"})},
                               {"x", ExpressionArg("coordinates: X, мм.")},
                               {"y", ExpressionArg("coordinates: Y, мм.")},
                               {"z", ExpressionArg("coordinates: Z, мм.")},
                               {"point", Point3Arg("vertex: вершина тела.")},
                               {"edge", IntegerArg("center/midpoint/intersection: ребро.")},
                               {"sphere", IntegerArg("center: сферическая грань.")},
                               {"axis", IdArg("intersection: рабочая ось вместо ребра.")},
                               {"plane", StringArg("intersection: плоскость.")}})),
                   true, &WorkPoint});
}

} // namespace bz::app::scripting
