// Operations that make solids from sketch profiles: extrude, revolve,
// sweep, coil; and a thread on a cylinder.

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "document/CoilFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/RevolveFeature.h"
#include "document/SketchFeature.h"
#include "document/SweepFeature.h"
#include "document/ThreadFeature.h"

#include <string>
#include <cmath>

#include <format>

namespace bz::app::scripting {

namespace {

const std::vector<std::string> kOperations = {"new_body", "join", "cut", "intersect"};

/// The shared part of extrude and revolve: sketch, profiles, boolean.
void FillSolid(Session& s, const Json& args, doc::SketchSolidFeature& f) {
    f.sketch = Id(args, "sketch");
    if (!s.doc.FindAs<doc::SketchFeature>(f.sketch)) Fail("Элемент «sketch» — не эскиз");
    // The first solid of a part makes a body; later ones join it by default.
    const bool hasBody = !s.model.Bodies().empty();
    f.operation = static_cast<doc::BooleanOp>(Choice(args, "operation", kOperations, hasBody ? 1 : 0));
    if (Has(args, "profiles")) {
        const Json& list = Required(args, "profiles");
        if (!list.is_array() || list.empty()) Fail("«profiles» — непустой список точек внутри контуров");
        f.profiles.automatic = false;
        for (const Json& p : list) f.profiles.regions.push_back(Point(p, "profiles"));
    }
}

Json Finish(Session& s, std::unique_ptr<doc::SketchSolidFeature> f, const Json& args, const char* title) {
    f->id   = s.doc.Ids().Next();
    f->name = Has(args, "name") ? Text(args, "name") : s.doc.UniqueFeatureName(Tr(title));
    const ObjectId id = f->id;
    s.Execute(doc::AddFeature(std::move(f), s.doc.EndMarker()));
    Json out = AfterChange(s, id);
    RollBackIfFailed(s, id);
    for (const Parameter* p : s.doc.Find(id)->Parameters())
        if (!p->name.empty()) out["parameters"].push_back({{"name", p->name}, {"expression", p->expression}});
    if (const auto props = s.model.Properties()) out["volume_mm3"] = props->volume;
    return out;
}

Json Extrude(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    auto f = std::make_unique<doc::ExtrudeFeature>();
    FillSolid(s, args, *f);
    static const std::vector<std::string> kExtents = {"distance", "symmetric", "through_all", "asymmetric"};
    // Choice gives the index in kExtents; ExtentType has ToPlane between them.
    const std::size_t extent = Choice(args, "extent", kExtents, 0);
    f->extent   = extent == 3 ? doc::ExtentType::Asymmetric : static_cast<doc::ExtentType>(extent);
    f->reversed = Flag(args, "reversed", false);
    // Names in a row: the extrusion is not in the document yet, so
    // NextParameterName would give each of its parameters the same one.
    const int first = std::stoi(s.doc.NextParameterName().substr(1));
    int given = 0;
    const auto nextName = [&] { return "d" + std::to_string(first + given++); };
    // Through all needs no distance: no named parameter that drives nothing.
    if (f->extent != doc::ExtentType::ThroughAll) {
        f->distance.name       = nextName();
        f->distance.expression = Expression(args, "distance");
    }
    if (f->extent == doc::ExtentType::Asymmetric) {   // the other side
        f->distance2.name       = nextName();
        f->distance2.expression = Expression(args, "distance2");
    }
    if (Has(args, "taper")) {   // named like the distance: part.set_parameter can change it
        f->taper.name       = nextName();
        f->taper.expression = Expression(args, "taper");
    }
    return Finish(s, std::move(f), args, "Выдавливание");
}

Json Revolve(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    auto f = std::make_unique<doc::RevolveFeature>();
    FillSolid(s, args, *f);
    const auto& sk = s.doc.FindAs<doc::SketchFeature>(f->sketch)->sketch;
    f->axis.kind = doc::RevolveAxisKind::SketchLine;
    if (Has(args, "axis")) {
        f->axis.line = Id(args, "axis");
        if (!sk.FindLine(f->axis.line)) Fail("«axis» — не отрезок этого эскиза");
    } else {
        for (const auto& l : sk.lines)
            if (l.centerline) f->axis.line = l.id;
        if (!f->axis.line) Fail("В эскизе нет осевой: нарисуйте её (sketch.add, type centerline) или укажите «axis»");
    }
    static const std::vector<std::string> kExtents = {"full", "angle", "symmetric"};
    f->extent   = static_cast<doc::RevolveExtent>(Choice(args, "extent", kExtents, Has(args, "angle") ? 1 : 0));
    f->reversed = Flag(args, "reversed", false);
    if (f->extent != doc::RevolveExtent::Full) {   // a full turn has no angle to drive
        f->angle.name       = s.doc.NextParameterName();
        f->angle.expression = Expression(args, "angle", "90");
    }
    return Finish(s, std::move(f), args, "Вращение");
}

Json Sweep(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    auto f = std::make_unique<doc::SweepFeature>();
    FillSolid(s, args, *f);
    f->path = Id(args, "path");
    if (!s.doc.FindAs<doc::SketchFeature>(f->path)) Fail("«path» — не эскиз");
    if (f->path == f->sketch) Fail("Траектория рисуется в другом эскизе, не в эскизе профиля");
    return Finish(s, std::move(f), args, "Сдвиг");
}

Json Coil(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    auto f = std::make_unique<doc::CoilFeature>();
    FillSolid(s, args, *f);
    const auto& sk = s.doc.FindAs<doc::SketchFeature>(f->sketch)->sketch;
    if (Has(args, "axis")) {
        f->axisLine = Id(args, "axis");
        if (!sk.FindLine(f->axisLine)) Fail("«axis» — не отрезок этого эскиза");
    } else {
        for (const auto& l : sk.lines)
            if (l.centerline) f->axisLine = l.id;
        if (!f->axisLine) Fail("В эскизе нет осевой: нарисуйте её (sketch.add, type centerline) или укажите «axis»");
    }
    f->leftHanded = Flag(args, "left_handed", false);
    f->pitch.name       = s.doc.NextParameterName();
    f->pitch.expression = Expression(args, "pitch");
    f->turns.name       = "d" + std::to_string(std::stoi(f->pitch.name.substr(1)) + 1);
    f->turns.expression = Expression(args, "turns");
    return Finish(s, std::move(f), args, "Пружина");
}

Vec3 Point3(const Json& args, const char* key, std::optional<Vec3> fallback) {
    if (!Has(args, key) && fallback) return *fallback;
    const Json& v = Required(args, key);
    if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number())
        Fail(std::format("«{}» — не точка [x, y, z]", key));
    return {Sane(v[0].get<double>(), key), Sane(v[1].get<double>(), key), Sane(v[2].get<double>(), key)};
}

/// A thread on the cylinder (shaft or hole) whose axis passes through
/// `axis_point` along `axis_dir`, of about `diameter`: the kernel finds the
/// face itself (doc::CylinderTouch), the same way a click in the viewport does.
Json Thread(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    if (s.model.Bodies().empty()) Fail("В детали нет тела, на котором делать резьбу");
    const Vec3   point    = Point3(args, "axis_point", std::nullopt);
    Vec3         dir      = Point3(args, "axis_dir", Vec3{0, 0, 1});
    const double diameter = Number(args, "diameter");
    if (dir.Length() < 1e-9) Fail("«axis_dir» — нулевой вектор");
    if (diameter <= 0) Fail("«diameter» должен быть больше нуля");
    dir = dir.Normalized();
    // Any unit vector across the axis (the thread does not use it).
    const Vec3 helper = std::abs(dir.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 radial = dir.Cross(helper).Normalized();

    auto f          = std::make_unique<doc::ThreadFeature>();
    f->id           = s.doc.Ids().Next();
    f->name         = Has(args, "name") ? Text(args, "name") : s.doc.UniqueFeatureName(Tr("Резьба"));
    f->face         = doc::CylinderTouch{point, dir, diameter / 2, radial};
    f->size         = Has(args, "size") ? Number(args, "size") : 0.0;
    f->fromOtherEnd = Flag(args, "from_other_end", false);
    f->leftHanded   = Flag(args, "left_handed", false);
    f->cosmetic     = Flag(args, "cosmetic", false);
    f->pitch.name   = s.doc.NextParameterName();
    const bool autoPitch = !Has(args, "pitch");
    f->pitch.expression = autoPitch ? "1" : Expression(args, "pitch");   // auto: set below from the size found
    if (Has(args, "length")) {
        f->fullLength = false;
        f->length.name       = "d" + std::to_string(std::stoi(f->pitch.name.substr(1)) + 1);
        f->length.expression = Expression(args, "length");
    }
    const ObjectId id = f->id;
    s.Execute(doc::AddFeature(std::move(f), s.doc.EndMarker()));
    s.RebuildNow();

    auto* made = s.doc.FindAs<doc::ThreadFeature>(id);
    if (!made->site) {   // no cylinder there: take the step back rather than leave a broken feature
        const std::string why = made->status.message;
        s.Undo();
        s.RebuildNow();
        Fail(std::format("Резьба не построена: {} (ось через [{}, {}, {}], Ø{}). Проверьте ось и диаметр по part.properties / sketch.get",
                         why.empty() ? "цилиндрическая грань не найдена" : why, point.x, point.y, point.z, diameter));
    }
    if (autoPitch) {
        // The coarse ISO pitch of the size, as the panel does after a click.
        // Still the same undo step: nothing else happened since it was added.
        const double nominal = made->size > 0 ? made->size : made->site->nominal;
        made->pitch.expression = std::format("{}", doc::CoarsePitch(nominal));
        s.doc.MarkChanged(static_cast<std::size_t>(s.doc.IndexOf(id)));
    }
    Json out = AfterChange(s, id);
    made     = s.doc.FindAs<doc::ThreadFeature>(id);
    if (made->site)
        out["thread"] = {{"external", made->site->external},
                         {"nominal_mm", made->size > 0 ? made->size : made->site->nominal},
                         {"pitch_mm", made->pitch.value},
                         {"face_length_mm", made->site->length},
                         {"designation", std::format("M{}x{}", made->size > 0 ? made->size : made->site->nominal, made->pitch.value)}};
    for (const Parameter* p : made->Parameters())
        if (!p->name.empty()) out["parameters"].push_back({{"name", p->name}, {"expression", p->expression}});
    if (const auto props = s.model.Properties()) out["volume_mm3"] = props->volume;
    return out;
}

Json SolidSchema(Json own, std::vector<std::string> required) {
    own["sketch"]    = IdArg("id эскиза с замкнутыми контурами.");
    own["operation"] = EnumArg("new_body — новое тело, join — добавить, cut — вырезать, intersect — пересечь. "
                               "По умолчанию: первое тело — new_body, дальше — join.", kOperations);
    own["profiles"]  = {{"type", "array"}, {"items", PointArg("Точка внутри контура.")},
                       {"description", "Какие контуры брать (точки из sketch.get → profiles.inside). Не задано — все внешние, отверстия остаются отверстиями."}};
    own["name"]      = StringArg("Имя операции.");
    required.insert(required.begin(), "sketch");
    return Schema(std::move(own), std::move(required));
}

} // namespace

void AddSolidCommands(Commands& out) {
    out.push_back({"feature.extrude",
                   "Выдавливание контуров эскиза по нормали к его плоскости. Расстояние становится параметром (dN). "
                   "Возвращает состояние операции (при ошибке — сообщение) и объём детали.",
                   SolidSchema({{"distance", ExpressionArg("Расстояние, мм.")},
                                {"extent", EnumArg("distance — на расстояние (по умолчанию), symmetric — в обе стороны поровну, "
                                                   "through_all — насквозь (для cut), asymmetric — distance в направлении и "
                                                   "distance2 в обратную сторону.",
                                                   {"distance", "symmetric", "through_all", "asymmetric"})},
                                {"distance2", ExpressionArg("asymmetric: расстояние в обратную сторону, мм.")},
                                {"reversed", BoolArg("В другую сторону.")},
                                {"taper", ExpressionArg("Уклон, градусы.")}},
                               {}),
                   true, &Extrude});
    out.push_back({"feature.revolve",
                   "Вращение контуров эскиза вокруг отрезка того же эскиза (по умолчанию — вокруг осевой). "
                   "Контур рисуйте по одну сторону от оси; осевая может быть его стороной.",
                   SolidSchema({{"axis", IdArg("id отрезка-оси; не задан — осевая эскиза.")},
                                {"extent", EnumArg("full — 360° (по умолчанию), angle — на угол, symmetric — угол поровну в обе стороны.",
                                                   {"full", "angle", "symmetric"})},
                                {"angle", ExpressionArg("Угол, градусы (для angle/symmetric).")},
                                {"reversed", BoolArg("В другую сторону.")}},
                               {}),
                   true, &Revolve});
    out.push_back({"feature.sweep",
                   "Сдвиг: замкнутые контуры эскиза sketch протягиваются вдоль траектории — цепочки отрезков и дуг в другом "
                   "эскизе path (обычно на перпендикулярной плоскости; профиль ставьте в начало траектории).",
                   SolidSchema({{"path", IdArg("id эскиза с траекторией.")}}, {"path"}),
                   true, &Sweep});
    out.push_back({"feature.coil",
                   "Пружина (спираль): контур эскиза навивается вокруг осевой того же эскиза с шагом pitch, turns витков. "
                   "Контур — по одну сторону от оси, не касаясь её; шаг больше высоты контура.",
                   SolidSchema({{"axis", IdArg("id отрезка-оси; не задан — осевая эскиза.")},
                                {"pitch", ExpressionArg("Шаг, мм.")},
                                {"turns", ExpressionArg("Число витков.")},
                                {"left_handed", BoolArg("Левая навивка.")}},
                               {"pitch", "turns"}),
                   true, &Coil});
    out.push_back({"feature.thread",
                   "Метрическая резьба ISO 60° (винтовая, настоящая) на цилиндрической грани: на валу — наружная, в отверстии — "
                   "внутренняя (отверстие = диаметр под метчик). Грань задаётся осью и диаметром: точка на оси, направление оси, "
                   "диаметр цилиндра (вал M8 — 8; отверстие под M8 — 6.8). Шаг по умолчанию — крупный по ISO для найденного размера. "
                   "Резьба идёт по всей длине грани или на length от её начала.",
                   Schema({{"axis_point", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3},
                                           {"description", "Любая точка на оси цилиндра [x, y, z], мм, в координатах модели."}}},
                           {"axis_dir", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3},
                                         {"description", "Направление оси [x, y, z]; по умолчанию [0, 0, 1]."}}},
                           {"diameter", NumberArg("Диаметр цилиндрической грани, мм.")},
                           {"pitch", ExpressionArg("Шаг, мм; не задан — крупный по ISO.")},
                           {"length", ExpressionArg("Длина резьбы, мм; не задана — по всей грани.")},
                           {"size", NumberArg("Номинал вручную (8 для M8), если грань другого диаметра; не задан — по грани.")},
                           {"from_other_end", BoolArg("Начать с другого конца грани.")},
                           {"left_handed", BoolArg("Левая резьба.")},
                           {"cosmetic", BoolArg("Условная: только рисуется на грани, модель лёгкая; в экспорт идёт настоящая.")},
                           {"name", StringArg("Имя операции.")}},
                          {"axis_point", "diameter"}),
                   true, &Thread});
}

} // namespace bz::app::scripting
