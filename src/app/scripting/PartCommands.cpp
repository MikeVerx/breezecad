// The part: its tree, parameters and properties, and edits that do not make
// new geometry (parameters, variables, names, suppression, deleting).

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "document/Commands.h"
#include "document/JsonIO.h"

#include <format>

namespace bz::app::scripting {

namespace {

std::string UnitOf(const UnitSystem& units, Quantity q) {
    if (q == Quantity::Length) return std::string(ToId(units.length));
    if (q == Quantity::Angle) return std::string(ToId(units.angle));
    return "";
}

Json DescribeParameter(const UnitSystem& units, const Parameter& p) {
    Json out = {{"name", p.name},
                {"expression", p.expression},
                {"value", units.ToDisplay(p.value, p.quantity)},
                {"unit", UnitOf(units, p.quantity)}};
    if (p.HasError()) out["error"] = Tr(p.error);
    return out;
}

Json Tree(AppContext& app, const Json&) {
    Session&             s = ActivePart(app);
    const doc::Document& d = s.doc;
    s.RebuildNow();
    Json features = Json::array();
    for (std::size_t i = 0; i < d.FeatureCount(); ++i) {
        const doc::Feature& f = *d.Features()[i];
        Json                item = Describe(f);
        item["index"] = i;
        if (!d.IsActive(i)) item["after_end_marker"] = true;
        Json deps = Json::array();
        for (ObjectId dep : f.Dependencies())
            if (dep) deps.push_back(dep.value);
        if (!deps.empty()) item["uses"] = std::move(deps);
        Json params = Json::array();
        for (const Parameter* p : f.Parameters())
            if (!p->name.empty()) params.push_back(DescribeParameter(d.units, *p));
        if (!params.empty()) item["parameters"] = std::move(params);
        features.push_back(std::move(item));
    }
    Json bodies = Json::array();
    for (const kernel::BodyInfo& b : s.model.Bodies()) bodies.push_back({{"id", b.id.value}, {"name", b.name}});
    return {{"title", s.Title()},
            {"name", d.info.name},
            {"designation", d.info.designation},
            {"material", d.info.material},
            {"units", {{"length", ToId(d.units.length)}, {"angle", ToId(d.units.angle)}}},
            {"features", std::move(features)},
            {"bodies", std::move(bodies)},
            {"origin", {{"planes", {{"XY", 2}, {"XZ", 3}, {"YZ", 4}}}, {"axes", {{"X", 5}, {"Y", 6}, {"Z", 7}}}}}};
}

Json FeatureFields(AppContext& app, const Json& args) {
    Session&            s = ActivePart(app);
    const doc::Feature& f = FeatureOf(s, Id(args, "id"));
    Json                out = Describe(f);
    Json                fields;
    f.SaveFields(fields);
    out["fields"] = std::move(fields);
    return out;
}

Json Parameters(AppContext& app, const Json&) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    Json out = Json::array();
    for (const Parameter& v : s.doc.variables) {
        Json p     = DescribeParameter(s.doc.units, v);
        p["owner"] = "variables";
        out.push_back(std::move(p));
    }
    for (const auto& f : s.doc.Features())
        for (const Parameter* param : f->Parameters()) {
            if (param->name.empty()) continue;
            Json p        = DescribeParameter(s.doc.units, *param);
            p["owner"]    = f->name;
            p["owner_id"] = f->id.value;
            out.push_back(std::move(p));
        }
    return out;
}

Json Properties(AppContext& app, const Json& args) {
    Session& s = ActivePart(app);
    s.RebuildNow();
    const auto props = s.model.Properties();
    if (!props) Fail("В детали ещё нет тел");
    Json out = {{"bodies", s.model.Bodies().size()},
                {"volume_mm3", props->volume},
                {"area_mm2", props->area},
                {"centre_mm", doc::json_io::ToJson(props->centre)},
                {"bounds_mm", {{"min", doc::json_io::ToJson(props->bounds.min)}, {"max", doc::json_io::ToJson(props->bounds.max)}}},
                {"size_mm", doc::json_io::ToJson(props->bounds.max - props->bounds.min)}};
    if (Has(args, "density")) {
        const double density = Number(args, "density");   // g/cm^3
        if (density <= 0) Fail("Плотность должна быть больше нуля");
        out["mass_g"] = props->volume / 1000.0 * density;
    }
    return out;
}

/// Sets the expression of the parameter called `name`: a user variable or
/// an input of a feature (one undo step either way).
Json SetParameter(AppContext& app, const Json& args) {
    Session&          s          = ActivePart(app);
    const std::string name       = Text(args, "name");
    const std::string expression = Expression(args, "value");

    for (std::size_t i = 0; i < s.doc.variables.size(); ++i) {
        if (s.doc.variables[i].name != name) continue;
        std::vector<Parameter> vars = s.doc.variables;
        vars[i].expression          = expression;
        s.Execute(doc::SetVariables(std::move(vars)));
        s.RebuildNow();
        s.doc.EvaluateParameters();   // a part without features has nothing to rebuild
        return DescribeParameter(s.doc.units, s.doc.variables[i]);
    }
    for (const auto& f : s.doc.Features()) {
        for (const Parameter* p : f->Parameters()) {
            if (p->name != name) continue;
            std::unique_ptr<doc::Feature> edited = f->Clone();
            std::vector<Parameter*>       params;
            edited->CollectParameters(params);
            for (Parameter* q : params)
                if (q->name == name) q->expression = expression;
            const ObjectId id = f->id;
            s.Execute(doc::ReplaceFeature(std::move(edited), Trf("Изменить {}", name)));
            Json out = AfterChange(s, id);
            for (const Parameter* q : s.doc.Find(id)->Parameters())
                if (q->name == name) out["parameter"] = DescribeParameter(s.doc.units, *q);
            return out;
        }
    }
    Fail(std::format("Нет параметра «{}» (список — part.parameters)", name));
}

Json SetVariable(AppContext& app, const Json& args) {
    Session&               s    = ActivePart(app);
    const std::string      name = Text(args, "name");
    std::vector<Parameter> vars = s.doc.variables;
    auto it = std::find_if(vars.begin(), vars.end(), [&](const Parameter& p) { return p.name == name; });
    if (it == vars.end()) {
        for (const Parameter* p : s.doc.AllParameters())
            if (p->name == name) Fail(std::format("Имя «{}» уже занято параметром операции", name));
        static const std::vector<std::string> kKinds = {"length", "angle", "scalar"};
        const std::size_t kind = Choice(args, "quantity", kKinds, 0);
        Parameter p;
        p.name     = name;
        p.quantity = kind == 0 ? Quantity::Length : kind == 1 ? Quantity::Angle : Quantity::Scalar;
        vars.push_back(p);
        it = vars.end() - 1;
    }
    it->expression = Expression(args, "value");
    const std::size_t index = static_cast<std::size_t>(it - vars.begin());
    s.Execute(doc::SetVariables(std::move(vars)));
    s.RebuildNow();
    s.doc.EvaluateParameters();   // a part without features has nothing to rebuild
    return DescribeParameter(s.doc.units, s.doc.variables[index]);
}

Json SetInfo(AppContext& app, const Json& args) {
    Session&      s    = ActivePart(app);
    doc::PartInfo info = s.doc.info;
    info.name          = Text(args, "name", info.name);
    info.designation   = Text(args, "designation", info.designation);
    info.material      = Text(args, "material", info.material);
    s.Execute(doc::SetPartInfo(info));
    return {{"name", info.name}, {"designation", info.designation}, {"material", info.material}};
}

/// One undoable edit of a feature's own fields (name, suppressed).
Json EditFeature(AppContext& app, const Json& args, bool rename) {
    Session&      s       = ActivePart(app);
    doc::Feature& current = FeatureOf(s, Id(args, "id"));
    auto          edited  = current.Clone();
    std::string   label;
    if (rename) {
        edited->name = Text(args, "name");
        if (edited->name.empty()) Fail("Имя не может быть пустым");
        label = Trf("Переименовать {}", current.name);
    } else {
        edited->suppressed = Flag(args, "suppressed", true);
        label = edited->suppressed ? Trf("Подавить {}", current.name) : Trf("Включить {}", current.name);
    }
    const ObjectId id = current.id;
    s.Execute(doc::ReplaceFeature(std::move(edited), label));
    return AfterChange(s, id);
}

Json Delete(AppContext& app, const Json& args) {
    Session&             s  = ActivePart(app);
    const doc::Feature&  f  = FeatureOf(s, Id(args, "id"));
    const std::string    name = f.name;
    s.Execute(doc::RemoveFeature(s.doc, f.id));
    s.RebuildNow();
    Json broken = Json::array();   // what used it and is now in error
    for (const auto& g : s.doc.Features())
        if (g->status.state == doc::FeatureState::Error) broken.push_back(Describe(*g));
    return {{"deleted", name}, {"errors_now", std::move(broken)}};
}

} // namespace

void AddPartCommands(Commands& out) {
    out.push_back({"part.tree",
                   "Дерево активной детали: операции по порядку (id, имя, тип, состояние ok/warning/error, сообщение, "
                   "какие элементы использует, именованные параметры), тела, id плоскостей и осей начала координат. "
                   "Начинайте работу с детали с этой команды.",
                   Schema(), false, &Tree});
    out.push_back({"part.feature", "Все поля одного элемента дерева (как в файле .bzc): для эскиза — точки, кривые, зависимости.",
                   Schema({{"id", IdArg("id элемента из part.tree.")}}, {"id"}), false, &FeatureFields});
    out.push_back({"part.parameters",
                   "Все именованные параметры детали: переменные пользователя и параметры операций и размеров эскизов "
                   "(имя, выражение, значение в единицах детали, чей).",
                   Schema(), false, &Parameters});
    out.push_back({"part.properties",
                   "МЦХ детали: объём, площадь, центр, габаритный параллелепипед; с плотностью (г/см³) — масса в граммах.",
                   Schema({{"density", NumberArg("Плотность материала, г/см³ (сталь 7.85, алюминий 2.7, PLA 1.24).")}}), false,
                   &Properties});
    out.push_back({"part.set_parameter",
                   "Меняет выражение именованного параметра (переменной, размера эскиза или параметра операции) и перестраивает деталь.",
                   Schema({{"name", StringArg("Имя параметра из part.parameters, например d3.")},
                           {"value", ExpressionArg("Новое значение.")}},
                          {"name", "value"}),
                   true, &SetParameter});
    out.push_back({"part.set_variable",
                   "Создаёт или меняет переменную пользователя (таблица переменных). На неё можно ссылаться в любых выражениях.",
                   Schema({{"name", StringArg("Имя переменной (латиница, без пробелов), например width.")},
                           {"value", ExpressionArg("Значение.")},
                           {"quantity", EnumArg("Что это за величина (для новой переменной), по умолчанию length.",
                                                {"length", "angle", "scalar"})}},
                          {"name", "value"}),
                   true, &SetVariable});
    out.push_back({"part.set_info", "Меняет свойства детали: наименование, обозначение, материал (текст).",
                   Schema({{"name", StringArg("Наименование.")},
                           {"designation", StringArg("Обозначение (децимальный номер).")},
                           {"material", StringArg("Материал, например «Сталь 20».")}}),
                   true, &SetInfo});
    out.push_back({"part.rename", "Переименовывает элемент дерева.",
                   Schema({{"id", IdArg("id элемента.")}, {"name", StringArg("Новое имя.")}}, {"id", "name"}), true,
                   [](AppContext& app, const Json& args) { return EditFeature(app, args, true); }});
    out.push_back({"part.suppress", "Подавляет (выключает) или включает операцию.",
                   Schema({{"id", IdArg("id элемента.")}, {"suppressed", BoolArg("true — подавить, false — включить; по умолчанию true.")}},
                          {"id"}),
                   true, [](AppContext& app, const Json& args) { return EditFeature(app, args, false); }});
    out.push_back({"part.delete",
                   "Удаляет элемент дерева. Операции, которые его использовали, станут ошибочными — их список возвращается.",
                   Schema({{"id", IdArg("id элемента.")}}, {"id"}), true, &Delete});
}

} // namespace bz::app::scripting
