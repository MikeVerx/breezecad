// Operations on the body: hole, fillet, chamfer, shell, patterns, mirror,
// loft, section. Faces and edges are numbers from part.faces / part.edges;
// planes are XY / XZ / YZ or a work plane's id.

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "document/EdgeFeature.h"
#include "document/HoleFeature.h"
#include "document/LoftFeature.h"
#include "document/Origin.h"
#include "document/PatternFeature.h"
#include "document/SectionFeature.h"
#include "document/ShellFeature.h"
#include "document/SketchFeature.h"
#include "document/SketchSolidFeature.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"

#include <cmath>
#include <format>
#include <type_traits>

namespace bz::app::scripting {

namespace {

doc::ModelAxis AxisArg(const Json& args, const char* key, doc::ModelAxis fallback) {
    static const std::vector<std::string> kAxes = {"X", "Y", "Z"};
    return static_cast<doc::ModelAxis>(Choice(args, key, kAxes, static_cast<std::size_t>(fallback)));
}

std::vector<ObjectId> Sources(Session& s, const Json& args) {
    const Json& list = Required(args, "sources");
    if (!list.is_array() || list.empty()) Fail("«sources» — непустой список id операций (выдавливание, вращение, отверстие)");
    std::vector<ObjectId> out;
    for (const Json& v : list) {
        const ObjectId id{v.get<std::uint64_t>()};
        FeatureOf(s, id);
        out.push_back(id);
    }
    return out;
}

const std::vector<std::string> kOperations = {"new_body", "join", "cut", "intersect"};

Json Hole(AppContext& app, const Json& args) {
    Session&                s    = ActivePart(app);
    const kernel::FaceInfo& face = FaceArg(s, Required(args, "face"), "face");
    if (face.kind != doc::SurfaceKind::Plane) Fail("Отверстие ставится на плоскую грань");
    auto f  = std::make_unique<doc::HoleFeature>();
    f->face = face.ref;
    const Frame frame = doc::PlaneFrame(face.ref);
    for (const Json& c : args.value("centers", Json::array())) {
        const Vec3 p = Point3(c, "centers");
        if (std::abs((p - frame.origin).Dot(frame.normal)) > 0.5) Fail("Центр отверстия не лежит на выбранной грани (проверьте z/нормаль)");
        f->centres.push_back(frame.ToLocal(p));
    }
    // Coaxial centres: on the axis of a round edge or a cylindrical face,
    // following it when the part changes (as "Соосно" in the panel).
    for (const Json& c : args.value("coaxial", Json::array())) {
        doc::HoleAxis axis;
        Vec3 point, dir;
        if (c.contains("edge")) {
            const kernel::EdgeInfo& e = EdgeArg(s, c["edge"], "coaxial.edge");
            if (e.kind != doc::CurveKind::Circle || !e.centre) Fail("«coaxial.edge» — круглое ребро");
            axis.edge = e.ref;
            point = *e.centre;
            dir = e.direction;
        } else if (c.contains("face")) {
            const kernel::FaceInfo& cyl = FaceArg(s, c["face"], "coaxial.face");
            if (cyl.kind != doc::SurfaceKind::Cylinder || !cyl.axis) Fail("«coaxial.face» — цилиндрическая грань");
            axis.cylinder = doc::CylinderTouch{cyl.axis->axisPoint, cyl.axis->axisDir, cyl.axis->radius, {}};
            point = cyl.axis->axisPoint;
            dir = cyl.axis->axisDir.Normalized();
        } else {
            Fail("«coaxial» — список {\"edge\": N} или {\"face\": N}");
        }
        if (std::abs(dir.Normalized().Dot(frame.normal)) < 0.999) Fail("Ось кромки или цилиндра не перпендикулярна грани отверстия");
        axis.centre = f->centres.size();
        f->centres.push_back(frame.ToLocal(point + dir * ((frame.origin - point).Dot(frame.normal) / dir.Dot(frame.normal))));
        f->concentric.push_back(axis);
    }
    if (f->centres.empty()) Fail("Нужен хотя бы один центр: «centers» (точки на грани) или «coaxial» (кромка или цилиндр)");
    static const std::vector<std::string> kTypes = {"simple", "counterbore", "countersink"};
    f->type       = static_cast<doc::HoleType>(Choice(args, "type", kTypes, 0));
    f->throughAll = Flag(args, "through_all", !Has(args, "depth"));
    f->diameter.expression = Expression(args, "diameter");
    if (Has(args, "depth")) f->depth.expression = Expression(args, "depth");
    if (Has(args, "head_diameter")) f->headDiameter.expression = Expression(args, "head_diameter");
    if (Has(args, "head_depth")) f->headDepth.expression = Expression(args, "head_depth");
    if (Has(args, "sink_diameter")) f->sinkDiameter.expression = Expression(args, "sink_diameter");
    std::vector<Parameter*> name{&f->diameter};
    if (!f->throughAll) name.push_back(&f->depth);
    if (f->type == doc::HoleType::Counterbore) name.insert(name.end(), {&f->headDiameter, &f->headDepth});
    if (f->type == doc::HoleType::Countersink) name.push_back(&f->sinkDiameter);
    return AddToPart(s, std::move(f), args, "Отверстие", name);
}

/// Фаска: equal legs (as before), a distance and an angle, or two distances;
/// `flip` turns the reference face of all edges, `flip_edges` of those only.
Json Chamfer(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::ChamferFeature>();
    f->edges   = EdgeRefs(s, args, "edges");
    static const std::vector<std::string> kModes = {"equal", "distance_angle", "two_distances"};
    f->mode = static_cast<doc::ChamferMode>(Choice(args, "mode", kModes, 0));
    f->distance.expression = Expression(args, "distance");
    std::vector<Parameter*> name{&f->distance};
    if (f->mode == doc::ChamferMode::DistanceAngle) {
        f->angle.expression = Expression(args, "angle");
        name.push_back(&f->angle);
    } else if (f->mode == doc::ChamferMode::TwoDistances) {
        f->distance2.expression = Expression(args, "distance2");
        name.push_back(&f->distance2);
    }
    f->flipped = Flag(args, "flip", false);
    if (Has(args, "flip_edges"))
        for (const doc::EdgeReference& e : EdgeRefs(s, args, "flip_edges")) {
            const auto at = std::ranges::find(f->edges, e);
            if (at == f->edges.end()) Fail("«flip_edges» — только рёбра из «edges»");
            f->FlipAt(static_cast<std::size_t>(at - f->edges.begin()));
        }
    return AddToPart(s, std::move(f), args, "Фаска", name);
}

template <class T>
Json EdgeOp(AppContext& app, const Json& args, const char* title, const char* valueKey) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<T>();
    f->edges   = EdgeRefs(s, args, "edges");
    Parameter* p = nullptr;
    if constexpr (std::is_same_v<T, doc::FilletFeature>) p = &f->radius;
    else p = &f->distance;
    p->expression = Expression(args, valueKey);
    return AddToPart(s, std::move(f), args, title, std::vector<Parameter*>{p});
}

Json Shell(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::ShellFeature>();
    if (Has(args, "open_faces")) f->faces = FaceRefs(s, args, "open_faces");
    f->thickness.expression = Expression(args, "thickness");
    f->outward = Flag(args, "outward", false);
    std::vector<Parameter*> name{&f->thickness};
    return AddToPart(s, std::move(f), args, "Оболочка", name);
}

Json Pattern(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::PatternFeature>();
    f->sources = Sources(s, args);
    static const std::vector<std::string> kKinds = {"rectangular", "circular"};
    f->kind = static_cast<doc::PatternKind>(Choice(args, "kind", kKinds, 0));
    std::vector<Parameter*> name;
    if (f->kind == doc::PatternKind::Rectangular) {
        f->direction1          = AxisArg(args, "direction1", doc::ModelAxis::X);
        f->reversed1           = Flag(args, "reversed1", false);
        f->count1.expression   = Expression(args, "count1");
        f->spacing1.expression = Expression(args, "spacing1");
        name = {&f->count1, &f->spacing1};
        if (Has(args, "count2")) {
            f->useDirection2       = true;
            f->direction2          = AxisArg(args, "direction2", doc::ModelAxis::Y);
            f->reversed2           = Flag(args, "reversed2", false);
            f->count2.expression   = Expression(args, "count2");
            f->spacing2.expression = Expression(args, "spacing2");
            name.insert(name.end(), {&f->count2, &f->spacing2});
        }
    } else {
        f->axis = AxisArg(args, "axis", doc::ModelAxis::Z);
        if (Has(args, "work_axis")) {
            f->workAxis = Id(args, "work_axis");
            if (!s.doc.FindAs<doc::WorkAxisFeature>(f->workAxis)) Fail("«work_axis» — не рабочая ось");
        }
        f->circularCount.expression = Expression(args, "count");
        f->angle.expression = Expression(args, "angle", "360");
        name = {&f->circularCount, &f->angle};
    }
    const char* title = f->kind == doc::PatternKind::Circular ? "Круговой массив" : "Массив";
    return AddToPart(s, std::move(f), args, title, name);
}

Json Mirror(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::MirrorFeature>();
    f->wholeBody = Flag(args, "whole_body", false);
    if (!f->wholeBody) f->sources = Sources(s, args);
    if (Has(args, "face")) {
        const kernel::FaceInfo& face = FaceArg(s, args["face"], "face");
        if (face.kind != doc::SurfaceKind::Plane) Fail("Зеркало — относительно плоской грани");
        f->face = face.ref;
    } else {
        f->plane = PlaneArg(s, args, "plane");
    }
    return AddToPart(s, std::move(f), args, "Зеркало");
}

Json Loft(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::LoftFeature>();
    const Json& list = Required(args, "sections");
    if (!list.is_array() || list.size() < 2) Fail("«sections» — минимум два эскиза, по порядку");
    for (const Json& v : list) {
        const ObjectId id{v.get<std::uint64_t>()};
        if (!s.doc.FindAs<doc::SketchFeature>(id)) Fail(std::format("{} — не эскиз", id.value));
        f->sections.push_back(id);
    }
    const bool hasBody = !s.model.Bodies().empty();
    f->operation = static_cast<doc::BooleanOp>(Choice(args, "operation", kOperations, hasBody ? 1 : 0));
    f->ruled     = Flag(args, "ruled", false);
    return AddToPart(s, std::move(f), args, "Лофт");
}

Json Section(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    auto     f = std::make_unique<doc::SectionFeature>();
    if (Has(args, "face")) {
        const kernel::FaceInfo& face = FaceArg(s, args["face"], "face");
        if (face.kind != doc::SurfaceKind::Plane) Fail("Сечение — плоской гранью или плоскостью");
        f->face = face.ref;
    } else {
        f->plane = PlaneArg(s, args, "plane");
    }
    f->flipped = Flag(args, "flipped", false);
    return AddToPart(s, std::move(f), args, "Сечение");
}

Json OpSchema(Json own, std::vector<std::string> required = {}) {
    own["name"] = StringArg("Имя операции в дереве.");
    return Schema(std::move(own), std::move(required));
}

} // namespace

void AddFeatureCommands(Commands& out) {
    out.push_back({"feature.hole",
                   "Отверстия на плоской грани (номер из part.faces) с центрами в точках модели: простое, с цековкой "
                   "(counterbore) или зенковкой (countersink); насквозь или на глубину. Резьбу в отверстии — потом "
                   "feature.thread по его оси и диаметру. Под метчик M8 — диаметр 6.8.",
                   OpSchema({{"face", IntegerArg("Плоская грань, на которой начинаются отверстия.")},
                             {"centers", {{"type", "array"}, {"items", Point3Arg("Центр.")}, {"description", "Центры на этой грани."}}},
                             {"coaxial",
                              {{"type", "array"},
                               {"items", {{"type", "object"},
                                          {"properties", {{"edge", IntegerArg("Круглое ребро (part.edges).")},
                                                          {"face", IntegerArg("Или цилиндрическая грань (part.faces).")}}}}},
                               {"description", "Центры соосно с кромкой или цилиндром: следуют за ними при изменении детали."}}},
                             {"diameter", ExpressionArg("Диаметр, мм.")},
                             {"depth", ExpressionArg("Глубина, мм; не задана — насквозь.")},
                             {"through_all", BoolArg("Насквозь.")},
                             {"type", EnumArg("simple (по умолчанию), counterbore, countersink.", {"simple", "counterbore", "countersink"})},
                             {"head_diameter", ExpressionArg("Цековка: диаметр.")},
                             {"head_depth", ExpressionArg("Цековка: глубина.")},
                             {"sink_diameter", ExpressionArg("Зенковка: диаметр на поверхности (конус 90°).")}},
                            {"face", "diameter"}),
                   true, &Hole});
    out.push_back({"feature.fillet", "Сопряжение (скругление) рёбер радиусом. Рёбра — номера из part.edges.",
                   OpSchema({{"edges", IndexListArg("Номера рёбер.")}, {"radius", ExpressionArg("Радиус, мм.")}}, {"edges", "radius"}), true,
                   [](AppContext& app, const Json& args) { return EdgeOp<doc::FilletFeature>(app, args, "Сопряжение", "radius"); }});
    out.push_back({"feature.chamfer",
                   "Фаска на рёбрах (номера из part.edges): equal — равные катеты distance (по умолчанию); distance_angle — "
                   "distance по опорной грани и angle (0..90°) между фаской и ею; two_distances — distance по опорной грани, "
                   "distance2 по другой. Опорная грань ребра — та, что смотрит выше по Z (затем Y, X); flip меняет её у всех "
                   "рёбер, flip_edges — только у этих.",
                   OpSchema({{"edges", IndexListArg("Номера рёбер.")},
                             {"mode", EnumArg("equal (по умолчанию), distance_angle, two_distances.", {"equal", "distance_angle", "two_distances"})},
                             {"distance", ExpressionArg("Катет (по опорной грани), мм.")},
                             {"distance2", ExpressionArg("two_distances: катет по другой грани, мм.")},
                             {"angle", ExpressionArg("distance_angle: угол к опорной грани, градусы.")},
                             {"flip", BoolArg("Опорная — другая грань у всех рёбер.")},
                             {"flip_edges", IndexListArg("Рёбра (из edges), у которых опорная грань другая.")}},
                            {"edges", "distance"}),
                   true, &Chamfer});
    out.push_back({"feature.shell",
                   "Оболочка: тело становится полым со стенкой thickness; open_faces — грани, которые убираются (открытые).",
                   OpSchema({{"open_faces", IndexListArg("Номера граней (part.faces), которые станут открытыми.")},
                             {"thickness", ExpressionArg("Толщина стенки, мм.")},
                             {"outward", BoolArg("Стенка наружу (по умолчанию внутрь).")}},
                            {"thickness"}),
                   true, &Shell});
    out.push_back({"feature.pattern",
                   "Массив операций (выдавливание, вращение, отверстие — их id в sources). rectangular: вдоль осей модели "
                   "direction1 (и direction2) count штук с шагом spacing; circular: count штук вокруг оси модели axis "
                   "(или рабочей оси work_axis) на угол angle (по умолчанию 360).",
                   OpSchema({{"sources", IndexListArg("id операций, которые повторить.")},
                             {"kind", EnumArg("rectangular (по умолчанию) или circular.", {"rectangular", "circular"})},
                             {"direction1", EnumArg("Ось первого направления.", {"X", "Y", "Z"})},
                             {"count1", ExpressionArg("Сколько штук по первому направлению (с исходной).")},
                             {"spacing1", ExpressionArg("Шаг, мм.")},
                             {"reversed1", BoolArg("В минус.")},
                             {"direction2", EnumArg("Ось второго направления.", {"X", "Y", "Z"})},
                             {"count2", ExpressionArg("Второе направление: штук (задан — направление включено).")},
                             {"spacing2", ExpressionArg("Второе направление: шаг, мм.")},
                             {"reversed2", BoolArg("Второе направление в минус.")},
                             {"axis", EnumArg("circular: ось модели (по умолчанию Z).", {"X", "Y", "Z"})},
                             {"work_axis", IdArg("circular: рабочая ось вместо оси модели.")},
                             {"count", ExpressionArg("circular: штук (с исходной).")},
                             {"angle", ExpressionArg("circular: угол, градусы.")}},
                            {"sources"}),
                   true, &Pattern});
    out.push_back({"feature.mirror",
                   "Зеркальная копия операций (sources) или всего тела (whole_body) относительно плоскости XY/XZ/YZ, "
                   "рабочей плоскости или плоской грани.",
                   OpSchema({{"sources", IndexListArg("id операций.")},
                             {"whole_body", BoolArg("Отразить всё тело — последнее созданное (Тело N с наибольшим номером); остальные тела не отражаются.")},
                             {"plane", StringArg("XY, XZ, YZ или имя/id рабочей плоскости.")},
                             {"face", IntegerArg("Или плоская грань (part.faces).")}}),
                   true, &Mirror});
    out.push_back({"feature.loft",
                   "Лофт: тело через замкнутые контуры нескольких эскизов по порядку (обычно на рабочих плоскостях "
                   "друг над другом), в каждом эскизе — один контур.",
                   OpSchema({{"sections", IndexListArg("id эскизов по порядку.")},
                             {"operation", EnumArg("new_body, join, cut, intersect.", kOperations)},
                             {"ruled", BoolArg("Прямые переходы между сечениями.")}},
                            {"sections"}),
                   true, &Loft});
    out.push_back({"feature.section",
                   "Сечение: модель отрезается плоскостью, одна сторона остаётся (по умолчанию отрезается сторона, куда "
                   "смотрит нормаль; flipped — наоборот). Выключается подавлением.",
                   OpSchema({{"plane", StringArg("XY, XZ, YZ или рабочая плоскость.")},
                             {"face", IntegerArg("Или плоская грань.")},
                             {"flipped", BoolArg("Оставить другую сторону.")}}),
                   true, &Section});
}

} // namespace bz::app::scripting
