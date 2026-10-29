// Sheet metal: «Лист», «Отбортовка», the flat pattern and its DXF
// (docs/ADR/0007-sheet-metal.md).

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/features/DxfEditor.h"
#include "app/features/SheetMetalEditors.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "core/PathText.h"
#include "document/Commands.h"
#include "document/SheetMetalFeature.h"
#include "document/SketchFeature.h"
#include "sheetmetal/Dxf.h"

#include <fstream>
#include <sstream>

namespace bz::app::scripting {
namespace {

Json Added(Session& s, ObjectId id) {
    Json out = AfterChange(s, id);
    RollBackIfFailed(s, id);
    if (const doc::Feature* f = s.doc.Find(id))
        for (const Parameter* p : f->Parameters())
            if (!p->name.empty()) out["parameters"].push_back({{"name", p->name}, {"expression", p->expression}});
    return out;
}

ObjectId SheetArg(Session& s, const Json& args) {
    if (Has(args, "sheet")) {
        const ObjectId id = Id(args, "sheet");
        if (!s.doc.FindAs<doc::SheetMetalFeature>(id)) Fail("«sheet» — не операция «Лист»");
        return id;
    }
    const ObjectId last = LastSheet(s.doc);
    if (!last) Fail("В детали нет листа: сначала sheet.create");
    return last;
}

Json CreateSheet(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    auto f = std::make_unique<doc::SheetMetalFeature>();
    f->sketch = Id(args, "sketch");
    if (!s.doc.FindAs<doc::SketchFeature>(f->sketch)) Fail("Элемент «sketch» — не эскиз");
    if (Has(args, "profiles")) {
        const Json& list = Required(args, "profiles");
        if (!list.is_array() || list.size() != 1) Fail("«profiles» — одна точка внутри контура листа");
        f->profiles.automatic = false;
        f->profiles.regions.push_back(Point(list[0], "profiles"));
    }
    f->thickness.name = s.doc.NextParameterName();
    f->thickness.expression = Expression(args, "thickness", "2");
    if (Has(args, "radius")) {
        f->radiusAuto = false;
        f->radius.expression = Expression(args, "radius");
    }
    if (Has(args, "k_factor")) {
        f->kAuto = false;
        f->kFactor.expression = Expression(args, "k_factor");
    }
    f->reversed = Flag(args, "reversed", false);
    f->id = s.doc.Ids().Next();
    f->name = Has(args, "name") ? Text(args, "name") : s.doc.UniqueFeatureName(Tr("Лист"));
    const ObjectId id = f->id;
    s.Execute(doc::AddFeature(std::move(f), s.doc.EndMarker()));
    Json out = Added(s, id);
    if (const auto* sheet = s.doc.FindAs<doc::SheetMetalFeature>(id)) {
        const sheet::Rules r = sheet->Rules();
        out["rules"] = {{"thickness", r.thickness}, {"radius", r.radius}, {"k_factor", r.kFactor},
                        {"radius_auto", sheet->radiusAuto}, {"k_auto", sheet->kAuto}};
    }
    return out;
}

Json Flange(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    auto f = std::make_unique<doc::FlangeFeature>();
    f->sheet = SheetArg(s, args);
    if (Has(args, "on_flange")) {
        f->onFlange = Id(args, "on_flange");
        const auto* under = s.doc.FindAs<doc::FlangeFeature>(f->onFlange);
        if (!under) Fail("«on_flange» — не операция «Отбортовка»");
        f->sheet = under->sheet;
    } else if (Has(args, "edge")) {
        f->edge = EdgeArg(s, Required(args, "edge"), "edge").middle;
        f->edgeSet = true;
    } else if (Has(args, "edge_point")) {
        f->edge = Point3(Required(args, "edge_point"), "edge_point");
        f->edgeSet = true;
    } else {
        Fail("Укажите ребро: «edge» (номер из part.edges), «edge_point» [x, y, z] — точку на ребре, или «on_flange» — id отбортовки");
    }
    if (f->onFlange) {
        static const std::vector<std::string> kEdges = {"far", "start", "end"};
        f->onEdge = static_cast<sheet::FlangeEdge>(Choice(args, "on_edge", kEdges, 0));
    } else if (Has(args, "width")) {
        f->widthMode = doc::FlangeWidth::Centered;
        f->width.expression = Expression(args, "width");
    } else if (Has(args, "offset_start") || Has(args, "offset_end")) {
        f->widthMode = doc::FlangeWidth::Offsets;
        f->offset1.expression = Expression(args, "offset_start", "0");
        f->offset2.expression = Expression(args, "offset_end", "0");
    }
    f->height.name = s.doc.NextParameterName();
    f->height.expression = Expression(args, "height", "20");
    f->angle.expression = Expression(args, "angle", "90");
    f->down = Flag(args, "down", false);
    f->id = s.doc.Ids().Next();
    f->name = Has(args, "name") ? Text(args, "name") : s.doc.UniqueFeatureName(Tr("Отбортовка"));
    const ObjectId id = f->id;
    s.Execute(doc::AddFeature(std::move(f), s.doc.EndMarker()));
    Json out = Added(s, id);
    if (const auto info = s.model.SheetAt(id); info && !info->layout.flanges.empty()) {
        const sheet::BentFlange& b = info->layout.flanges.back();
        out["flange"] = {{"straight_mm", b.straight}, {"bend_allowance_mm", b.allowance}, {"setback_mm", b.setback}};
    }
    if (const auto props = s.model.Properties()) out["volume_mm3"] = props->volume;
    return out;
}

Json SheetCut(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    auto f = std::make_unique<doc::SheetCutFeature>();
    f->sheet = SheetArg(s, args);
    f->sketch = Id(args, "sketch");
    if (!s.doc.FindAs<doc::SketchFeature>(f->sketch)) Fail("Элемент «sketch» — не эскиз");
    f->operation = doc::BooleanOp::Cut;
    if (Has(args, "profiles")) {
        const Json& list = Required(args, "profiles");
        if (!list.is_array() || list.empty()) Fail("«profiles» — непустой список точек внутри контуров");
        f->profiles.automatic = false;
        for (const Json& p : list) f->profiles.regions.push_back(Point(p, "profiles"));
    }
    f->id = s.doc.Ids().Next();
    f->name = Has(args, "name") ? Text(args, "name") : s.doc.UniqueFeatureName(Tr("Вырез"));
    const ObjectId id = f->id;
    s.Execute(doc::AddFeature(std::move(f), s.doc.EndMarker()));
    Json out = Added(s, id);
    if (const auto props = s.model.Properties()) out["volume_mm3"] = props->volume;
    return out;
}

Json FlatPattern(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    const ObjectId base = SheetArg(s, args);
    const auto info = s.model.Sheet(s.doc, base);
    if (!info) Fail("Лист не построен: исправьте ошибки в дереве");
    const sheet::FlatPattern flat = sheet::Flatten(info->part, info->layout);
    const Box2 box = flat.Bounds();
    Json out{{"width_mm", box.max.x - box.min.x}, {"height_mm", box.max.y - box.min.y}, {"area_mm2", flat.Area()},
             {"thickness_mm", flat.thickness}, {"radius_mm", info->part.rules.radius}, {"k_factor", info->part.rules.kFactor}};
    out["bends"] = Json::array();
    for (const auto& b : flat.bends) out["bends"].push_back(sheet::BendNote(b));
    if (Has(args, "dxf")) {
        const std::filesystem::path path = PathFromUtf8(Text(args, "dxf"));
        std::ofstream file(path, std::ios::binary);
        file << sheet::FlatPatternDxf(flat);
        if (!file) Fail("Не удалось записать " + Text(args, "dxf"));
        out["dxf"] = Text(args, "dxf");
    }
    if (Flag(args, "show", false)) app.shell.showFlatPattern = true;
    return out;
}


Json CheckJson(const dxf::Check& c) {
    Json ends = Json::array();
    for (const Vec2 p : c.openEnds) ends.push_back({p.x, p.y});
    return {{"open_ends", ends}, {"duplicates", c.duplicates.size()}, {"closed_contours", c.closedContours}};
}

Json DxfOpen(AppContext& app, const Json& args) {
    if (Flag(args, "flat_pattern", false)) {
        Session& s = ActivePart(app);
        s.RebuildNow();
        const auto info = s.model.Sheet(s.doc, SheetArg(s, args));
        if (!info) Fail("Лист не построен: исправьте ошибки в дереве");
        OpenDxfEditor(app, sheet::FlatPatternDrawing(sheet::Flatten(info->part, info->layout)), s.Title() + " развёртка");
    } else {
        std::string error;
        if (!OpenDxfEditorFile(app, PathFromUtf8(Text(args, "path")), &error)) Fail(error);
    }
    const dxf::Drawing* d = DxfEditorDrawing();
    Json layers = Json::array();
    for (const auto& l : d->layers) layers.push_back({{"name", l.name}, {"color", l.color}, {"visible", l.visible}});
    return {{"entities", d->entities.size()}, {"layers", layers}};
}

Json DxfCheck(AppContext& app, const Json& args) {
    (void)app;
    if (Has(args, "path")) {
        std::ifstream in(PathFromUtf8(Text(args, "path")), std::ios::binary);
        if (!in) Fail("Не удалось открыть " + Text(args, "path"));
        std::stringstream buffer;
        buffer << in.rdbuf();
        const auto d = dxf::Read(buffer.str());
        if (!d) Fail(d.GetError().message);
        bool cut = false;
        for (const auto& en : d->entities) cut = cut || en.layer == dxf::kCut;
        return CheckJson(dxf::CheckForCutting(*d, Text(args, "layer", cut ? dxf::kCut : "")));
    }
    if (!DxfEditorDrawing()) Fail("Редактор DXF не открыт: dxf.open или укажите path");
    return CheckJson(CheckInDxfEditor());
}

Json DxfSave(AppContext& app, const Json& args) {
    std::string error;
    if (!SaveDxfEditor(app, PathFromUtf8(Text(args, "path")), &error)) Fail(error);
    return {{"saved", Text(args, "path")}};
}

Json DxfSelect(AppContext& app, const Json& args) {
    (void)app;
    if (!DxfEditorDrawing()) Fail("Редактор DXF не открыт");
    return {{"selected", SelectInDxfEditor(Text(args, "layer", ""))}};
}

} // namespace

void AddSheetCommands(Commands& out) {
    out.push_back({"dxf.open",
                   "Открыть «Редактор DXF» (подготовка к резке) с файлом path или с развёрткой листа (flat_pattern: true). "
                   "Возвращает число объектов и слои.",
                   Schema({{"path", StringArg("DXF-файл.")}, {"flat_pattern", BoolArg("Открыть развёртку листа детали.")},
                           {"sheet", IdArg("id «Листа» для flat_pattern; не задан — последний.")}}),
                   false, &DxfOpen});
    out.push_back({"dxf.check",
                   "Проверка для резки: разрывы контуров (open_ends — координаты), наложенные дубли, число замкнутых контуров. "
                   "path — проверить файл; без path — чертёж в открытом редакторе (отметки появятся на нём).",
                   Schema({{"path", StringArg("DXF-файл.")}, {"layer", StringArg("Слой; по умолчанию CUT, если есть, иначе все.")}}),
                   false, &DxfCheck});
    out.push_back({"dxf.save", "Сохранить чертёж из редактора DXF в path (DXF R12, открывается в AutoCAD).",
                   Schema({{"path", StringArg("Куда сохранить.")}}, {"path"}), false, &DxfSave});
    out.push_back({"dxf.select", "Выбрать в редакторе DXF всё на слое layer (все объекты, если не задан).",
                   Schema({{"layer", StringArg("Слой.")}}), false, &DxfSelect});

    out.push_back({"sheet.create",
                   "Листовая деталь: один замкнутый контур эскиза становится листом толщины thickness. Радиус гиба и K-фактор "
                   "по умолчанию автоматические (R = толщине, K по DIN 6935) и следуют за толщиной; radius или k_factor задают "
                   "их вручную. Толщина — параметр (dN): part.set_parameter меняет её, всё остальное пересчитывается.",
                   Schema({{"sketch", IdArg("id эскиза с замкнутым контуром.")},
                           {"thickness", ExpressionArg("Толщина листа, мм (по умолчанию 2).")},
                           {"radius", ExpressionArg("Внутренний радиус гиба, мм; не задан — равен толщине.")},
                           {"k_factor", ExpressionArg("K-фактор 0…1; не задан — по DIN 6935.")},
                           {"reversed", BoolArg("Толщина вниз от плоскости эскиза.")},
                           {"profiles", {{"type", "array"}, {"items", PointArg("Точка внутри контура.")}, {"description", "Одна точка внутри контура листа, если контуров несколько."}}},
                           {"name", StringArg("Имя операции.")}},
                          {"sketch"}),
                   true, &CreateSheet});
    out.push_back({"sheet.flange",
                   "Отбортовка: полка на прямом ребре плоского листа или на дальнем ребре другой полки (on_flange), на всю длину ребра. Высота — наружная, от точки "
                   "пересечения наружных сторон (для 90° — наружный размер полки); основание укорачивается так, что наружный "
                   "габарит по эскизу сохраняется. Возвращает прямую часть полки и длину сгиба в развёртке.",
                   Schema({{"sheet", IdArg("id «Листа»; не задан — последний.")},
                           {"edge", IntegerArg("Номер ребра из part.edges (верхнее или нижнее ребро стороны листа).")},
                           {"edge_point", Point3Arg("Или точка на ребре [x, y, z], мм.")},
                           {"on_edge", EnumArg("С on_flange: far — дальнее ребро полки (по умолчанию), start / end — её боковые рёбра (гнётся от самого ребра).", {"far", "start", "end"})},
                           {"width", ExpressionArg("На ребре листа: ширина полки по центру ребра, мм.")},
                           {"offset_start", ExpressionArg("На ребре листа: отступ от начала ребра, мм (0 или не меньше толщины — рельеф).")},
                           {"offset_end", ExpressionArg("На ребре листа: отступ от конца ребра, мм.")},
                           {"on_flange", IdArg("Или id «Отбортовки»: новая встанет на её дальнее ребро (отбортовка на отбортовке; «вверх» — к внутренней стороне той полки).")},
                           {"height", ExpressionArg("Высота, мм (по умолчанию 20).")},
                           {"angle", ExpressionArg("Угол сгиба, градусы (по умолчанию 90).")},
                           {"down", BoolArg("Вниз, на другую сторону листа.")},
                           {"name", StringArg("Имя операции.")}}),
                   true, &Flange});
    out.push_back({"sheet.cut",
                   "Вырез сквозь лист: замкнутые контуры эскиза, который лежит на грани плоской части листа или полки "
                   "(sketch.create на грани из part.faces). Попадает в развёртку на своё место. Через сгиб и край — пока нельзя.",
                   Schema({{"sketch", IdArg("id эскиза на грани листа или полки.")},
                           {"sheet", IdArg("id «Листа»; не задан — последний.")},
                           {"profiles", {{"type", "array"}, {"items", PointArg("Точка внутри контура.")}, {"description", "Какие контуры; не задано — все."}}},
                           {"name", StringArg("Имя операции.")}},
                          {"sketch"}),
                   true, &SheetCut});
    out.push_back({"sheet.flat_pattern",
                   "Развёртка листовой детали: размеры заготовки, площадь, сгибы («ВВЕРХ 90° R2»). dxf — путь, куда сохранить "
                   "DXF R12, открывается в AutoCAD (слои CUT — рез, BEND — гибы). show — открыть окно «Развёртка».",
                   Schema({{"sheet", IdArg("id «Листа»; не задан — последний.")},
                           {"dxf", StringArg("Куда сохранить DXF.")},
                           {"show", BoolArg("Показать окно «Развёртка».")}}),
                   false, &FlatPattern});
}

} // namespace bz::app::scripting
