#include "app/scripting/Scripting.h"

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/features/OperationEditors.h"
#include "app/scripting/Commands.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "document/Feature.h"

#include <cmath>
#include <format>

namespace bz::app::scripting {

namespace {

const Commands& Table() {
    static const Commands table = [] {
        Commands all;
        AddDocumentCommands(all);
        AddPartCommands(all);
        AddSketchCommands(all);
        AddSolidCommands(all);
        AddTopologyCommands(all);
        AddGenericCommands(all);
        return all;
    }();
    return table;
}

Json List() {
    Json out = Json::array();
    out.push_back({{"name", "commands.list"},
                   {"description", "Список всех команд с описаниями и JSON-схемами аргументов."},
                   {"inputSchema", Schema()},
                   {"changes", false}});
    for (const Command& c : Table())
        out.push_back({{"name", c.name}, {"description", c.description}, {"inputSchema", c.schema}, {"changes", c.changes}});
    return out;
}

/// Why a change cannot be made now; empty when it can.
std::string EditInProgress(AppContext& app) {
    const Session* s = app.Doc();
    if (!s) return {};
    if (app.ops.Active()) return "Пользователь сейчас редактирует операцию в BreezeCAD — попросите его завершить (Enter) или отменить (Esc)";
    if (s->mode == EditMode::Sketch) return "Пользователь сейчас редактирует эскиз в BreezeCAD — попросите его выйти из эскиза";
    if (s->mode == EditMode::PickPlane) return "Пользователь сейчас выбирает плоскость эскиза в BreezeCAD — попросите его закончить или нажать Esc";
    return {};
}

Json Reply(const Json& request, AppContext& app) {
    if (!request.is_object()) Fail("Запрос — не JSON-объект");
    const std::string name = Text(request, "command");
    const Json        args = request.contains("args") && !request["args"].is_null() ? request["args"] : Json::object();
    if (!args.is_object()) Fail("«args» — не объект");
    if (name == "commands.list") return List();
    for (const Command& c : Table()) {
        if (c.name != name) continue;
        if (c.changes)
            if (const std::string busy = EditInProgress(app); !busy.empty()) Fail(busy);
        return c.run(app, args);
    }
    Fail(std::format("Нет команды «{}» (список — commands.list)", name));
}

} // namespace

std::string Run(AppContext& app, std::string_view request) {
    Json reply;
    try {
        const Json parsed = Json::parse(request);
        reply = {{"ok", true}, {"result", Reply(parsed, app)}};
    } catch (const Failure& f) {
        reply = {{"ok", false}, {"error", f.message}};
    } catch (const Json::exception& e) {
        reply = {{"ok", false}, {"error", std::format("Неправильный JSON: {}", e.what())}};
    } catch (const std::exception& e) {
        log::Warn("Command failed: {}", e.what());
        reply = {{"ok", false}, {"error", std::format("Ошибка программы: {}", e.what())}};
    }
    return reply.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// --- helpers ------------------------------------------------------------------

void Fail(std::string message) { throw Failure{std::move(message)}; }

Json Schema(Json properties, std::vector<std::string> required) {
    Json s = {{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty()) s["required"] = std::move(required);
    return s;
}
Json NumberArg(std::string d) { return {{"type", "number"}, {"description", std::move(d)}}; }
Json IntegerArg(std::string d) { return {{"type", "integer"}, {"description", std::move(d)}}; }
Json StringArg(std::string d) { return {{"type", "string"}, {"description", std::move(d)}}; }
Json BoolArg(std::string d) { return {{"type", "boolean"}, {"description", std::move(d)}}; }
Json EnumArg(std::string d, std::vector<std::string> values) {
    return {{"type", "string"}, {"enum", std::move(values)}, {"description", std::move(d)}};
}
Json ExpressionArg(std::string d) {
    return {{"type", {"number", "string"}},
            {"description", std::move(d) + " Число или выражение в единицах детали (мм, градусы): 25, \"w / 2\"."}};
}
Json PointArg(std::string d) {
    return {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2},
            {"description", std::move(d) + " [x, y] в мм, в координатах эскиза."}};
}
Json IdArg(std::string d) { return {{"type", "integer"}, {"description", std::move(d)}}; }

const Json& Required(const Json& args, const char* key) {
    const auto it = args.find(key);
    if (it == args.end() || it->is_null()) Fail(std::format("Не задан аргумент «{}»", key));
    return *it;
}

bool Has(const Json& args, const char* key) { return args.contains(key) && !args[key].is_null(); }

double Number(const Json& args, const char* key, std::optional<double> fallback) {
    if (!Has(args, key) && fallback) return *fallback;
    const Json& v = Required(args, key);
    if (!v.is_number()) Fail(std::format("«{}» — не число", key));
    return v.get<double>();
}

int Integer(const Json& args, const char* key, std::optional<int> fallback) {
    if (!Has(args, key) && fallback) return *fallback;
    const Json& v = Required(args, key);
    if (!v.is_number_integer()) Fail(std::format("«{}» — не целое число", key));
    return v.get<int>();
}

std::string Text(const Json& args, const char* key, std::optional<std::string> fallback) {
    if (!Has(args, key) && fallback) return *fallback;
    const Json& v = Required(args, key);
    if (!v.is_string()) Fail(std::format("«{}» — не строка", key));
    return v.get<std::string>();
}

bool Flag(const Json& args, const char* key, bool fallback) {
    if (!Has(args, key)) return fallback;
    if (!args[key].is_boolean()) Fail(std::format("«{}» — не true/false", key));
    return args[key].get<bool>();
}

ObjectId Id(const Json& args, const char* key) {
    const Json& v = Required(args, key);
    if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<long long>() > 0))
        Fail(std::format("«{}» — не id (целое положительное)", key));
    return ObjectId{v.get<std::uint64_t>()};
}

Vec2 Point(const Json& v, const char* what) {
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number())
        Fail(std::format("«{}» — не точка [x, y]", what));
    return {v[0].get<double>(), v[1].get<double>()};
}

Vec2 Point(const Json& args, const char* key, std::optional<Vec2> fallback) {
    if (!Has(args, key) && fallback) return *fallback;
    return Point(Required(args, key), key);
}

std::string Expression(const Json& args, const char* key, std::optional<std::string> fallback) {
    if (!Has(args, key) && fallback) return *fallback;
    const Json& v = Required(args, key);
    if (v.is_number()) return std::format("{}", v.get<double>());
    if (v.is_string() && !v.get<std::string>().empty()) return v.get<std::string>();
    Fail(std::format("«{}» — не число и не выражение", key));
}

std::size_t Choice(const Json& args, const char* key, const std::vector<std::string>& values, std::size_t fallback) {
    if (!Has(args, key)) return fallback;
    const std::string v = Text(args, key);
    for (std::size_t i = 0; i < values.size(); ++i)
        if (values[i] == v) return i;
    std::string all;
    for (const auto& x : values) all += (all.empty() ? "" : ", ") + x;
    Fail(std::format("«{}»: нет варианта «{}» (есть: {})", key, v, all));
}

Session& Active(AppContext& app) {
    Session* s = app.Doc();
    if (!s || app.workspace.ShowsHome()) Fail("Нет открытого документа (создайте деталь: app.new_part, или откройте файл: app.open)");
    return *s;
}

Session& ActivePart(AppContext& app) {
    Session& s = Active(app);
    if (s.IsAssembly()) Fail("Активный документ — сборка; команда работает с деталью");
    return s;
}

doc::Feature& FeatureOf(Session& part, ObjectId id) {
    doc::Feature* f = part.doc.Find(id);
    if (!f) Fail(std::format("В детали нет элемента с id {}", id.value));
    return *f;
}

const kernel::Topology& TopologyOf(Session& part) {
    // One part at a time is asked about; keyed by the model and its revision.
    static const kernel::Model* model = nullptr;
    static std::uint64_t        revision = ~std::uint64_t{0};
    static kernel::Topology     topology;
    part.RebuildNow();
    if (model != &part.model || revision != part.model.Revision()) {
        topology = kernel::DescribeTopology(part.model);
        model    = &part.model;
        revision = part.model.Revision();
    }
    return topology;
}

const kernel::FaceInfo& FaceArg(Session& part, const Json& value, const char* what) {
    if (!value.is_number_integer()) Fail(std::format("«{}» — номер грани из part.faces", what));
    const kernel::Topology& t = TopologyOf(part);
    const long long i = value.get<long long>();
    if (i < 0 || static_cast<std::size_t>(i) >= t.faces.size())
        Fail(std::format("Нет грани {} (в детали {} граней; список — part.faces, номера меняются после каждой правки)", i, t.faces.size()));
    return t.faces[static_cast<std::size_t>(i)];
}

std::vector<doc::EdgeReference> EdgeRefs(Session& part, const Json& args, const char* key) {
    const Json& list = Required(args, key);
    if (!list.is_array() || list.empty()) Fail(std::format("«{}» — непустой список номеров рёбер из part.edges", key));
    const kernel::Topology& t = TopologyOf(part);
    std::vector<doc::EdgeReference> out;
    for (const Json& v : list) {
        if (!v.is_number_integer()) Fail(std::format("«{}»: номера рёбер — целые числа", key));
        const long long i = v.get<long long>();
        if (i < 0 || static_cast<std::size_t>(i) >= t.edges.size())
            Fail(std::format("Нет ребра {} (в детали {} рёбер; список — part.edges, номера меняются после каждой правки)", i, t.edges.size()));
        out.push_back(t.edges[static_cast<std::size_t>(i)].ref);
    }
    return out;
}

std::vector<doc::FaceReference> FaceRefs(Session& part, const Json& args, const char* key) {
    const Json& list = Required(args, key);
    if (!list.is_array() || list.empty()) Fail(std::format("«{}» — непустой список номеров граней из part.faces", key));
    std::vector<doc::FaceReference> out;
    for (const Json& v : list) out.push_back(FaceArg(part, v, key).ref);
    return out;
}

Vec3 Point3(const Json& v, const char* what) {
    if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number())
        Fail(std::format("«{}» — не точка [x, y, z]", what));
    return {v[0].get<double>(), v[1].get<double>(), v[2].get<double>()};
}

Json ToJson3(Vec3 v) {
    const auto r = [](double x) { return std::abs(x) < 5e-7 ? 0.0 : std::round(x * 1e6) / 1e6; };
    return Json::array({r(v.x), r(v.y), r(v.z)});
}

Json Point3Arg(std::string d) {
    return {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3},
            {"description", std::move(d) + " [x, y, z] в мм, координаты модели (Z вверх)."}};
}

Json IndexListArg(std::string d) { return {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", std::move(d)}}; }

Json Describe(const doc::Feature& f) {
    static constexpr const char* kStates[] = {"not_computed", "ok", "warning", "error", "suppressed"};
    Json out = {{"id", f.id.value},
                {"name", f.name},
                {"type", std::string(f.Type())},
                {"state", kStates[static_cast<int>(f.status.state)]},
                {"suppressed", f.suppressed}};
    if (!f.status.message.empty()) out["message"] = Tr(f.status.message);
    return out;
}

Json AfterChange(Session& part, ObjectId feature) {
    part.RebuildNow();
    part.selectedFeature = feature;
    if (const doc::Feature* f = part.doc.Find(feature)) return Describe(*f);
    return Json::object();
}

} // namespace bz::app::scripting
