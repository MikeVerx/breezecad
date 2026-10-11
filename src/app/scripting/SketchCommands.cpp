// Sketches: create one on a plane, add geometry (with its dimensions, so the
// result is parametric at once), add constraints, read the solved result.
// Every call is one undo step.

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/scripting/Commands.h"
#include "app/viewport/Planes.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "document/SketchFeature.h"
#include "sketch/Shapes.h"

#include <cmath>
#include <format>

namespace bz::app::scripting {

namespace {

using sketch::Constraint;
using sketch::ConstraintType;

doc::SketchFeature& SketchOf(Session& s, const Json& args) {
    auto* f = dynamic_cast<doc::SketchFeature*>(&FeatureOf(s, Id(args, "sketch")));
    if (!f) Fail("Элемент «sketch» — не эскиз");
    return *f;
}

/// What sketch.add and sketch.constrain build on: a copy of the sketch
/// feature that becomes one undo step when everything went well.
class SketchEdit {
public:
    SketchEdit(Session& s, const doc::SketchFeature& f)
        : mySession(s), myFeature(static_cast<doc::SketchFeature*>(f.Clone().release())), myEdited(myFeature) {
        // Dimension names continue after the largest "dN" of the document.
        const std::string next = s.doc.NextParameterName();
        myNextName = std::stoi(next.substr(1));
    }

    sketch::Sketch& Sk() { return myFeature->sketch; }
    IdGenerator&    Ids() { return mySession.doc.Ids(); }

    /// An existing point at `pos` (within 1 µm), else a new one: shapes drawn
    /// end to end share their corners and close into profiles.
    ObjectId PointAt(Vec2 pos) {
        for (const sketch::Point& p : Sk().points)
            if (!p.projected && (p.pos - pos).Length() < 1e-3) return p.id;
        return Sk().AddPoint(Ids(), pos);
    }

    /// A driving dimension with the next free name ("d7") at the current size.
    ObjectId Dimension(ConstraintType type, ObjectId a, ObjectId b, double value, Vec2 labelOffset,
                       Quantity quantity = Quantity::Length) {
        Constraint c;
        c.type                 = type;
        c.refs                 = {a, b, {}};
        c.parameter.name       = "d" + std::to_string(myNextName++);
        c.parameter.quantity   = quantity;
        c.parameter.value      = value;
        c.parameter.expression = std::format("{}", std::round(mySession.doc.units.ToDisplay(value, quantity) * 1e6) / 1e6);
        c.labelOffset          = labelOffset;
        return Sk().AddConstraint(Ids(), c);
    }

    std::string NameOf(ObjectId constraint) const {
        const Constraint* c = myFeature->sketch.FindConstraint(constraint);
        return c ? c->parameter.name : "";
    }

    Json Commit(std::string label) {
        const ObjectId id = myFeature->id;
        mySession.Execute(doc::ReplaceFeature(std::move(myEdited), std::move(label)));
        return AfterChange(mySession, id);
    }

private:
    Session&                      mySession;
    doc::SketchFeature*           myFeature;
    std::unique_ptr<doc::Feature> myEdited;
    int                           myNextName = 1;
};

Vec2 Outward(Vec2 a, Vec2 b) {   // a dimension label beside the segment a-b
    const Vec2 d = b - a;
    return Vec2{-d.y, d.x}.Normalized() * 8.0;
}

/// Adds one item of sketch.add; returns what was made (ids, dimension names).
Json AddItem(SketchEdit& e, const Json& item) {
    sketch::Sketch&   sk   = e.Sk();
    const std::string type = Text(item, "type");
    const bool        dims = Flag(item, "dimensions", true);
    const bool        aux  = Flag(item, "construction", false);
    Json              out  = {{"type", type}};
    Json              names = Json::array();
    const auto        dim  = [&](ObjectId c) { names.push_back(e.NameOf(c)); };

    if (type == "line" || type == "centerline" || type == "polyline") {
        std::vector<Vec2> pts;
        if (type == "polyline") {
            const Json& list = Required(item, "points");
            if (!list.is_array() || list.size() < 2) Fail("polyline: «points» — минимум две точки");
            for (const Json& p : list) pts.push_back(Point(p, "points"));
        } else {
            pts = {Point(item, "from", std::nullopt), Point(item, "to", std::nullopt)};
        }
        const bool closed = type == "polyline" && Flag(item, "closed", false);
        std::vector<ObjectId> ids;
        for (Vec2 p : pts) ids.push_back(e.PointAt(p));
        if (closed) ids.push_back(ids.front());
        Json lines = Json::array();
        for (std::size_t i = 0; i + 1 < ids.size(); ++i) {
            if (ids[i] == ids[i + 1]) Fail("Отрезок нулевой длины");
            const ObjectId l = sk.AddLine(e.Ids(), ids[i], ids[i + 1], aux || type == "centerline");
            if (type == "centerline") sk.FindLine(l)->centerline = true;
            const Vec2 a = sk.PositionOf(ids[i]), b = sk.PositionOf(ids[i + 1]);
            if (dims && type != "centerline") dim(e.Dimension(ConstraintType::Length, l, {}, (b - a).Length(), Outward(a, b)));
            lines.push_back(l.value);
        }
        out["lines"] = std::move(lines);
    } else if (type == "circle") {
        const ObjectId c = e.PointAt(Point(item, "center", std::nullopt));
        const double   r = Has(item, "diameter") ? Number(item, "diameter") / 2 : Number(item, "radius");
        if (r <= 0) Fail("circle: размер должен быть больше нуля");
        const ObjectId circle = sk.AddCircle(e.Ids(), c, r, aux);
        if (dims) dim(e.Dimension(ConstraintType::Diameter, circle, {}, 2 * r, Vec2{1, 1}.Normalized() * (r + 8)));
        out["circle"] = circle.value;
        out["center"] = c.value;
    } else if (type == "arc") {
        const Vec2 cp = Point(item, "center", std::nullopt), sp = Point(item, "start", std::nullopt);
        Vec2       ep = Point(item, "end", std::nullopt);
        const double r = (sp - cp).Length();
        if (r <= 0) Fail("arc: начало совпадает с центром");
        ep = cp + (ep - cp).Normalized() * r;   // the end on the same radius
        const ObjectId arc = sk.AddArc(e.Ids(), e.PointAt(cp), e.PointAt(sp), e.PointAt(ep), aux);
        if (dims) dim(e.Dimension(ConstraintType::Radius, arc, {}, r, Vec2{1, 1}.Normalized() * (r + 8)));
        out["arc"] = arc.value;
    } else if (type == "rectangle") {
        sketch::RectangleIds r;
        if (Has(item, "center")) {
            const Vec2 c = Point(item, "center", std::nullopt);
            const double w = Number(item, "width"), h = Number(item, "height");
            if (w <= 0 || h <= 0) Fail("rectangle: ширина и высота должны быть больше нуля");
            r = sketch::AddCentredRectangle(sk, e.Ids(), e.PointAt(c), c + Vec2{w / 2, h / 2});
        } else {
            const Vec2 a = Point(item, "corner", std::nullopt), b = Point(item, "opposite", std::nullopt);
            if (std::abs(a.x - b.x) < 1e-6 || std::abs(a.y - b.y) < 1e-6) Fail("rectangle: углы на одной линии");
            r = sketch::AddCornerRectangle(sk, e.Ids(), e.PointAt(a), e.PointAt(b));
        }
        for (int i = 0; i < 2 && dims; ++i) {
            const sketch::Line* l = sk.FindLine(r.sides[static_cast<std::size_t>(i)]);
            const Vec2 a = sk.PositionOf(l->start), b = sk.PositionOf(l->end);
            dim(e.Dimension(ConstraintType::Length, l->id, {}, (b - a).Length(), Outward(a, b)));
        }
        out["sides"] = Json::array();
        for (ObjectId s : r.sides) out["sides"].push_back(s.value);
        out["corners"] = Json::array();
        for (ObjectId c : r.corners) out["corners"].push_back(c.value);
    } else if (type == "slot") {
        const Vec2   a = Point(item, "from", std::nullopt), b = Point(item, "to", std::nullopt);
        const double w = Number(item, "width");
        if (w <= 0 || (b - a).Length() < 1e-6) Fail("slot: нужны два разных центра и ширина больше нуля");
        const sketch::SlotIds slot = sketch::AddStraightSlot(sk, e.Ids(), e.PointAt(a), e.PointAt(b), w);
        if (dims) {
            dim(e.Dimension(ConstraintType::Length, slot.axis, {}, (b - a).Length(), Outward(a, b)));
            dim(e.Dimension(ConstraintType::Diameter, slot.cap, {}, w, Vec2{1, 1}.Normalized() * (w / 2 + 8)));
        }
        out["curves"] = Json::array();
        for (ObjectId c : slot.curves) out["curves"].push_back(c.value);
    } else if (type == "polygon") {
        const Vec2   c      = Point(item, "center", std::nullopt);
        const int    sides  = Integer(item, "sides");
        const double radius = Number(item, "radius");
        const bool   inside = Flag(item, "inscribed", true);
        if (sides < 3 || sides > 64 || radius <= 0) Fail("polygon: от 3 до 64 сторон, радиус больше нуля");
        const sketch::PolygonIds p = sketch::AddPolygon(sk, e.Ids(), e.PointAt(c), c + Vec2{radius, 0}, sides, inside);
        if (dims) dim(e.Dimension(ConstraintType::Diameter, p.circle, {}, 2 * radius, Vec2{1, 1}.Normalized() * (radius + 8)));
        out["sides"] = Json::array();
        for (ObjectId s : p.sides) out["sides"].push_back(s.value);
    } else {
        Fail(std::format("Неизвестный тип «{}» (line, centerline, polyline, rectangle, circle, arc, slot, polygon)", type));
    }
    if (!names.empty()) out["dimensions"] = std::move(names);
    return out;
}

Json Create(AppContext& app, const Json& args) {
    Session&          s     = ActivePart(app);
    const std::string plane = Text(args, "plane", "XY");
    std::optional<ShownPlane> chosen;
    for (const ShownPlane& p : OriginPlanes(s))
        if (p.label == plane || std::to_string(p.id.value) == plane) chosen = p;
    for (const ShownPlane& p : WorkPlanes(s))
        if (p.label == plane || std::to_string(p.id.value) == plane) chosen = p;
    if (!chosen) Fail(std::format("Нет плоскости «{}» (XY, XZ, YZ или имя/id рабочей плоскости)", plane));

    auto f       = std::make_unique<doc::SketchFeature>();
    f->id        = s.doc.Ids().Next();
    f->name      = Has(args, "name") ? Text(args, "name") : s.doc.UniqueFeatureName(Tr("Эскиз"));
    f->placement = {chosen->id, chosen->frame, false, {}};
    f->sketch.EnsureOrigin(s.doc.Ids());
    const ObjectId id = f->id;
    s.Execute(doc::AddFeature(std::move(f), s.doc.EndMarker()));
    Json out           = AfterChange(s, id);
    const Frame& frame = chosen->frame;
    out["plane"] = {{"name", chosen->label},
                    {"sketch_x_in_model", {frame.xDir.x, frame.xDir.y, frame.xDir.z}},
                    {"sketch_y_in_model", {frame.YDir().x, frame.YDir().y, frame.YDir().z}},
                    {"normal", {frame.normal.x, frame.normal.y, frame.normal.z}}};
    return out;
}

Json Add(AppContext& app, const Json& args) {
    Session&    s     = ActivePart(app);
    SketchEdit  edit(s, SketchOf(s, args));
    const Json& items = Required(args, "items");
    if (!items.is_array() || items.empty()) Fail("«items» — непустой список фигур");
    Json made = Json::array();
    for (const Json& item : items) {
        if (!item.is_object()) Fail("Элемент «items» — не объект");
        made.push_back(AddItem(edit, item));
    }
    Json out    = edit.Commit(Tr("Изменить эскиз"));
    out["made"] = std::move(made);
    return out;
}

Json Constrain(AppContext& app, const Json& args) {
    Session&   s = ActivePart(app);
    SketchEdit edit(s, SketchOf(s, args));
    Constraint c;
    if (!doc::ConstraintTypeFromId(Text(args, "type"), c.type)) Fail(std::format("Нет связи «{}»", Text(args, "type")));
    const Json& refs = Required(args, "refs");
    if (!refs.is_array() || refs.empty() || refs.size() > 3) Fail("«refs» — от одного до трёх id элементов эскиза");
    for (std::size_t i = 0; i < refs.size(); ++i) {
        c.refs[i] = ObjectId{refs[i].get<std::uint64_t>()};
        if (edit.Sk().KindOf(c.refs[i]) == sketch::EntityKind::None) Fail(std::format("В эскизе нет элемента {}", c.refs[i].value));
    }
    if (c.type == ConstraintType::Fix) c.fixedAt = edit.Sk().PositionOf(c.refs[0]);
    ObjectId added;
    if (sketch::IsDimensional(c.type)) {
        const Quantity q = c.type == ConstraintType::Angle ? Quantity::Angle : Quantity::Length;
        added = edit.Dimension(c.type, c.refs[0], c.refs[1], 0.0, {8, 8}, q);
        sketch::Constraint* made = edit.Sk().FindConstraint(added);
        made->refs               = c.refs;
        made->parameter.expression = Expression(args, "value");
        made->reference          = Flag(args, "reference", false);
    } else {
        added = edit.Sk().AddConstraint(edit.Ids(), c);
    }
    const std::string name = edit.NameOf(added);
    Json out = edit.Commit(Tr("Изменить эскиз"));
    out["constraint"] = added.value;
    if (!name.empty()) out["parameter"] = name;
    return out;
}

Json Remove(AppContext& app, const Json& args) {
    Session&    s = ActivePart(app);
    SketchEdit  edit(s, SketchOf(s, args));
    const Json& ids = Required(args, "entities");
    if (!ids.is_array() || ids.empty()) Fail("«entities» — непустой список id");
    for (const Json& v : ids) {
        const ObjectId id{v.get<std::uint64_t>()};
        if (edit.Sk().KindOf(id) == sketch::EntityKind::None) Fail(std::format("В эскизе нет элемента {}", id.value));
        edit.Sk().Remove(id);
    }
    return edit.Commit(Tr("Изменить эскиз"));
}

Json Get(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    const doc::SketchFeature& f  = SketchOf(s, args);
    const sketch::Sketch&     sk = f.sketch;
    Json out = Describe(f);
    const auto xy = [](Vec2 p) { return Json::array({std::round(p.x * 1e6) / 1e6, std::round(p.y * 1e6) / 1e6}); };
    out["degrees_of_freedom"] = f.analysis.degreesOfFreedom;
    Json pts = Json::array(), lines = Json::array(), circles = Json::array(), arcs = Json::array(), cons = Json::array();
    for (const auto& p : sk.points) {
        Json j = {{"id", p.id.value}, {"at", xy(p.pos)}};
        if (p.isOrigin) j["origin"] = true;
        pts.push_back(std::move(j));
    }
    for (const auto& l : sk.lines) {
        Json j = {{"id", l.id.value}, {"start", l.start.value}, {"end", l.end.value},
                  {"from", xy(sk.PositionOf(l.start))}, {"to", xy(sk.PositionOf(l.end))}};
        if (l.construction) j["construction"] = true;
        if (l.centerline) j["centerline"] = true;
        lines.push_back(std::move(j));
    }
    for (const auto& c : sk.circles)
        circles.push_back({{"id", c.id.value}, {"center", c.center.value}, {"at", xy(sk.PositionOf(c.center))}, {"radius", c.radius}});
    for (const auto& a : sk.arcs)
        arcs.push_back({{"id", a.id.value}, {"center", a.center.value}, {"start", a.start.value}, {"end", a.end.value},
                        {"radius", sk.RadiusOf(a.id)}});
    for (const auto& c : sk.constraints) {
        Json j = {{"id", c.id.value}, {"type", std::string(doc::ConstraintTypeId(c.type))}, {"refs", Json::array()}};
        for (ObjectId r : c.refs)
            if (r) j["refs"].push_back(r.value);
        if (sketch::IsDimensional(c.type)) j["parameter"] = {{"name", c.parameter.name}, {"expression", c.parameter.expression}};
        if (std::find(f.analysis.failing.begin(), f.analysis.failing.end(), c.id) != f.analysis.failing.end()) j["conflict"] = true;
        cons.push_back(std::move(j));
    }
    out["points"] = std::move(pts);
    out["lines"] = std::move(lines);
    out["circles"] = std::move(circles);
    out["arcs"] = std::move(arcs);
    out["constraints"] = std::move(cons);
    if (!sk.splines.empty() || !sk.ellipses.empty() || !sk.texts.empty())
        out["other"] = {{"splines", sk.splines.size()}, {"ellipses", sk.ellipses.size()}, {"texts", sk.texts.size()}};
    Json regions = Json::array();
    if (const auto* rs = s.model.Regions(f.id))
        for (const auto& r : *rs) regions.push_back({{"inside", xy(r.sample)}, {"holes", r.loops.size() - 1}, {"automatic", r.automatic}});
    out["profiles"] = std::move(regions);
    return out;
}

Json ItemSchema() {
    return {{"type", "object"},
            {"description",
             "Фигура. type: line {from,to} | centerline {from,to} — осевая для вращения | polyline {points,closed} | "
             "rectangle {corner,opposite} или {center,width,height} | circle {center, radius или diameter} | "
             "arc {center,start,end} — против часовой | slot {from,to,width} | polygon {center,sides,radius,inscribed}. "
             "Общие: construction (вспомогательная), dimensions (по умолчанию true — проставить размеры, "
             "они станут параметрами d1, d2…). Точки с одинаковыми координатами объединяются, так контуры замыкаются."},
            {"properties",
             {{"type", {{"type", "string"}, {"enum", {"line", "centerline", "polyline", "rectangle", "circle", "arc", "slot", "polygon"}}}},
              {"from", PointArg("Начало.")}, {"to", PointArg("Конец.")}, {"points", {{"type", "array"}, {"items", PointArg("Точка.")}}},
              {"closed", BoolArg("polyline: замкнуть.")}, {"corner", PointArg("Угол.")}, {"opposite", PointArg("Противоположный угол.")},
              {"center", PointArg("Центр.")}, {"width", NumberArg("Ширина, мм.")}, {"height", NumberArg("Высота, мм.")},
              {"radius", NumberArg("Радиус, мм.")}, {"diameter", NumberArg("Диаметр, мм.")}, {"start", PointArg("Начало дуги.")},
              {"end", PointArg("Конец дуги (ставится на радиус).")}, {"sides", IntegerArg("Число сторон.")},
              {"inscribed", BoolArg("polygon: вписанный (по умолчанию) или описанный.")},
              {"construction", BoolArg("Вспомогательная геометрия.")}, {"dimensions", BoolArg("Проставить размеры (по умолчанию true).")}}},
            {"required", {"type"}}};
}

} // namespace

void AddSketchCommands(Commands& out) {
    out.push_back({"sketch.create",
                   "Создаёт пустой эскиз на плоскости начала координат (XY — вид сверху, XZ — спереди, YZ — справа) "
                   "или на рабочей плоскости. Возвращает id эскиза и как оси эскиза лежат в модели. Модель — Z вверх.",
                   Schema({{"plane", StringArg("XY, XZ, YZ или имя/id рабочей плоскости; по умолчанию XY.")},
                           {"name", StringArg("Имя эскиза; по умолчанию «ЭскизN».")}}),
                   true, &Create});
    out.push_back({"sketch.add",
                   "Добавляет в эскиз фигуры одним шагом отмены. Координаты — мм в системе эскиза, начало эскиза (0,0) — "
                   "проекция начала координат. По умолчанию проставляет размеры — они становятся параметрами (d1…), "
                   "их потом меняет part.set_parameter. Возвращает id созданного и имена размеров.",
                   Schema({{"sketch", IdArg("id эскиза.")}, {"items", {{"type", "array"}, {"items", ItemSchema()}}}}, {"sketch", "items"}),
                   true, &Add});
    out.push_back({"sketch.constrain",
                   "Добавляет связь или размер между элементами эскиза (id из sketch.get). Геометрические: coincident, "
                   "horizontal, vertical, parallel, perpendicular, equal, fix, pointOnCurve, midpoint, tangent, concentric, "
                   "symmetric (точка, точка, линия). Размеры (нужен value): distance, distanceX, distanceY (две точки), "
                   "length (отрезок), pointLineDistance, radius, diameter, angle (два отрезка).",
                   Schema({{"sketch", IdArg("id эскиза.")},
                           {"type", StringArg("Тип связи (см. описание).")},
                           {"refs", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "id элементов эскиза, 1–3."}}},
                           {"value", ExpressionArg("Значение размера.")},
                           {"reference", BoolArg("Справочный размер (ничего не задаёт).")}},
                          {"sketch", "type", "refs"}),
                   true, &Constrain});
    out.push_back({"sketch.remove", "Удаляет элементы эскиза (точку, кривую, связь) вместе с тем, что от них зависит.",
                   Schema({{"sketch", IdArg("id эскиза.")},
                           {"entities", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "id элементов."}}}},
                          {"sketch", "entities"}),
                   true, &Remove});
    out.push_back({"sketch.get",
                   "Решённый эскиз: точки, отрезки, окружности, дуги с id и координатами; связи и размеры (конфликтующие "
                   "помечены), число степеней свободы, замкнутые контуры (profiles: точка внутри каждого — для выдавливания).",
                   Schema({{"sketch", IdArg("id эскиза.")}}, {"sketch"}), false, &Get});
}

} // namespace bz::app::scripting
