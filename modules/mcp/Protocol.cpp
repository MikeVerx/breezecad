#include "Protocol.h"

#include <array>
#include <stdexcept>

namespace bzmcp {

using Json = nlohmann::json;

namespace {

constexpr std::array<const char*, 3> kVersions = {"2025-06-18", "2025-03-26", "2024-11-05"};

// For the model: how to work with BreezeCAD through these tools.
constexpr const char* kInstructions =
    "BreezeCAD — параметрическая САПР деталей (как Inventor/КОМПАС). Инструменты работают с документом, "
    "открытым у пользователя: он видит каждое изменение, любое можно отменить (doc_undo или Ctrl+Z).\n"
    "Порядок работы: part_tree — что в детали; app_new_part — новая деталь. Тело строится так: "
    "sketch_create (плоскость XY/XZ/YZ, рабочая плоскость или плоская грань; модель — Z вверх) → sketch_add "
    "(фигуры в мм; размеры проставляются сами и становятся параметрами d1, d2…) → feature_extrude, "
    "feature_revolve (нужна осевая, type centerline), feature_sweep, feature_loft, feature_coil. "
    "Дальше — по граням и рёбрам: part_faces / part_edges дают их номера (с фильтрами: normal, kind, "
    "radius, near…), их принимают feature_hole, feature_fillet, feature_chamfer, feature_shell, "
    "feature_thread, sketch_create face, work_plane / work_axis / work_point. Номера действуют до "
    "следующего изменения детали — после правки запросите заново. Массивы и зеркало — feature_pattern, "
    "feature_mirror; любая другая операция — part_feature_types + part_create_feature. "
    "Операция, которая не построилась, сразу отменяется, а в ответе — причина. Размеры меняются "
    "part_set_parameter / part_edit_feature; общие размеры удобно завести переменными (part_set_variable). "
    "Проверка: part_properties (габариты, объём, масса), part_measure, view_snapshot — картинка модели. "
    "Стандартные изделия (болты, гайки, подшипники) — library_list / library_sizes / library_make_part; "
    "цвет и материал — appearance_list, part_set_appearance. Сохранение — doc_save, экспорт — doc_export "
    "(STEP/STL/3MF). Если команда отказала, потому что пользователь сейчас редактирует эскиз или "
    "операцию, попросите его закончить.";

Json RpcError(const Json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

Json RpcResult(const Json& id, Json result) { return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}; }

Json Text(const std::string& text, bool isError) {
    return {{"content", Json::array({{{"type", "text"}, {"text", text}}})}, {"isError", isError}};
}

} // namespace

Protocol::Protocol(std::function<std::string(const std::string&)> execute, std::string hostVersion)
    : myExecute(std::move(execute)), myHostVersion(std::move(hostVersion)) {}

std::string Protocol::ToolName(const std::string& command) {
    std::string out = command;
    for (char& c : out)
        if (c == '.') c = '_';
    return out;
}

std::optional<std::string> Protocol::Handle(const std::string& body) {
    const Json message = Json::parse(body, nullptr, false);
    if (message.is_discarded()) return RpcError(nullptr, -32700, "Parse error").dump();
    if (message.is_array()) {   // a batch (older protocol versions)
        Json replies = Json::array();
        for (const Json& m : message) {
            bool       reply = false;
            const Json r     = HandleOne(m, reply);
            if (reply) replies.push_back(r);
        }
        if (replies.empty()) return std::nullopt;
        return replies.dump();
    }
    bool       reply = false;
    const Json r     = HandleOne(message, reply);
    if (!reply) return std::nullopt;
    return r.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Json Protocol::HandleOne(const Json& m, bool& reply) {
    reply = false;
    if (!m.is_object() || m.value("jsonrpc", "") != "2.0") {
        reply = true;
        return RpcError(nullptr, -32600, "Invalid Request");
    }
    if (!m.contains("method")) return {};   // a response to us; we send no requests
    reply = m.contains("id");
    if (!reply) return {};                   // notifications: initialized, cancelled
    const Json&        id     = m["id"];
    const std::string  method = m["method"].is_string() ? m["method"].get<std::string>() : "";
    const Json         params = m.contains("params") && m["params"].is_object() ? m["params"] : Json::object();
    try {
        if (method == "initialize") return RpcResult(id, Initialize(params));
        if (method == "ping") return RpcResult(id, Json::object());
        if (method == "tools/list") return RpcResult(id, ListTools());
        if (method == "tools/call") return RpcResult(id, CallTool(params));
        if (method == "resources/list") return RpcResult(id, {{"resources", Json::array()}});
        if (method == "prompts/list") return RpcResult(id, {{"prompts", Json::array()}});
        return RpcError(id, -32601, "Method not found: " + method);
    } catch (const std::exception& e) {
        return RpcError(id, -32603, e.what());
    }
}

Json Protocol::Initialize(const Json& params) const {
    std::string version = kVersions[0];
    const std::string asked = params.value("protocolVersion", "");
    for (const char* v : kVersions)
        if (asked == v) version = v;
    return {{"protocolVersion", version},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "breezecad"}, {"title", "BreezeCAD"}, {"version", myHostVersion}}},
            {"instructions", kInstructions}};
}

Json Protocol::Host(const std::string& command, const Json& args) {
    const Json request = {{"command", command}, {"args", args}};
    const Json reply   = Json::parse(myExecute(request.dump()), nullptr, false);
    if (reply.is_discarded() || !reply.is_object()) return {{"ok", false}, {"error", "BreezeCAD ответил не JSON"}};
    return reply;
}

Json Protocol::ListTools() {
    const Json reply = Host("commands.list", Json::object());
    if (!reply.value("ok", false)) throw std::runtime_error(reply.value("error", "commands.list failed"));
    myCommands.clear();
    Json tools = Json::array();
    for (const Json& c : reply["result"]) {
        const std::string command = c.value("name", "");
        if (command.empty() || command == "commands.list") continue;
        const std::string name = ToolName(command);
        myCommands[name]       = command;
        Json tool = {{"name", name}, {"description", c.value("description", "")}, {"inputSchema", c.value("inputSchema", Json::object())}};
        const bool changes   = c.value("changes", false);
        tool["annotations"] = {{"readOnlyHint", !changes}, {"destructiveHint", false}, {"openWorldHint", false}};
        tools.push_back(std::move(tool));
    }
    return {{"tools", std::move(tools)}};
}

Json Protocol::CallTool(const Json& params) {
    const std::string name = params.value("name", "");
    if (myCommands.empty()) ListTools();   // a client that skipped tools/list
    const auto it = myCommands.find(name);
    if (it == myCommands.end()) return Text("Нет инструмента «" + name + "»", true);
    const Json args  = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : Json::object();
    const Json reply = Host(it->second, args);
    if (!reply.value("ok", false)) return Text(reply.value("error", "Ошибка"), true);
    Json result = reply["result"];
    // A picture (view.snapshot) goes as an image the model can see, the rest as text.
    if (result.is_object() && result.contains("image_png_base64")) {
        Json image = {{"type", "image"}, {"data", result["image_png_base64"]}, {"mimeType", "image/png"}};
        result.erase("image_png_base64");
        return {{"content", Json::array({std::move(image), {{"type", "text"}, {"text", result.dump(1, ' ', false, Json::error_handler_t::replace)}}})},
                {"isError", false}};
    }
    return Text(result.is_string() ? result.get<std::string>() : result.dump(1, ' ', false, Json::error_handler_t::replace), false);
}

} // namespace bzmcp
