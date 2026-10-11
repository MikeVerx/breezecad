// Operations that make solids from sketch profiles: extrude, revolve.

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "document/ExtrudeFeature.h"
#include "document/RevolveFeature.h"
#include "document/SketchFeature.h"

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
    static const std::vector<std::string> kExtents = {"distance", "symmetric", "through_all"};
    f->extent   = static_cast<doc::ExtentType>(Choice(args, "extent", kExtents, 0));
    f->reversed = Flag(args, "reversed", false);
    f->distance.name       = s.doc.NextParameterName();
    f->distance.expression = Expression(args, "distance", f->extent == doc::ExtentType::ThroughAll ? std::optional<std::string>("10") : std::nullopt);
    if (Has(args, "taper")) f->taper.expression = Expression(args, "taper");
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
    f->angle.name       = s.doc.NextParameterName();
    f->angle.expression = Expression(args, "angle", "90");
    return Finish(s, std::move(f), args, "Вращение");
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
                                                   "through_all — насквозь (для cut).", {"distance", "symmetric", "through_all"})},
                                {"reversed", BoolArg("В другую сторону.")},
                                {"taper", ExpressionArg("Уклон, градусы.")}},
                               {}),
                   true, &Extrude});
    out.push_back({"feature.revolve",
                   "Вращение контуров эскиза вокруг отрезка того же эскиза (по умолчанию — вокруг осевой). "
                   "Контур рисуйте по одну сторону от оси.",
                   SolidSchema({{"axis", IdArg("id отрезка-оси; не задан — осевая эскиза.")},
                                {"extent", EnumArg("full — 360° (по умолчанию), angle — на угол, symmetric — угол поровну в обе стороны.",
                                                   {"full", "angle", "symmetric"})},
                                {"angle", ExpressionArg("Угол, градусы (для angle/symmetric).")},
                                {"reversed", BoolArg("В другую сторону.")}},
                               {}),
                   true, &Revolve});
}

} // namespace bz::app::scripting
