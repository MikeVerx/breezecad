// The MCP module's protocol layer (modules/mcp/Protocol), with a fake host.

#include "Protocol.h"

#include <doctest.h>
#include <nlohmann/json.hpp>

#include <string>

using Json = nlohmann::json;

namespace {

/// A host with two commands; records what it was asked.
struct FakeHost {
    std::string last;
    std::string operator()(const std::string& request) {
        last = request;
        const Json r = Json::parse(request);
        if (r["command"] == "commands.list")
            return Json{{"ok", true},
                        {"result",
                         {{{"name", "commands.list"}, {"description", "list"}, {"inputSchema", {{"type", "object"}}}},
                          {{"name", "part.tree"}, {"description", "tree"}, {"inputSchema", {{"type", "object"}}}, {"changes", false}},
                          {{"name", "part.set_parameter"}, {"description", "set"}, {"inputSchema", {{"type", "object"}}}, {"changes", true}}}}}
                .dump();
        if (r["command"] == "part.tree") return R"({"ok":true,"result":{"features":[]}})";
        return R"({"ok":false,"error":"Нет параметра «x»"})";
    }
};

Json Call(bzmcp::Protocol& p, const std::string& method, Json params = Json::object(), int id = 1) {
    const auto reply = p.Handle(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}.dump());
    REQUIRE(reply);
    return Json::parse(*reply);
}

} // namespace

TEST_CASE("MCP: initialize, notifications, ping") {
    FakeHost        host;
    bzmcp::Protocol p([&](const std::string& r) { return host(r); }, "0.2.0");

    Json init = Call(p, "initialize", {{"protocolVersion", "2025-03-26"}});
    CHECK(init["result"]["protocolVersion"] == "2025-03-26");
    CHECK(init["result"]["serverInfo"]["name"] == "breezecad");
    CHECK(init["result"]["capabilities"].contains("tools"));
    CHECK(Call(p, "initialize", {{"protocolVersion", "1999-01-01"}})["result"]["protocolVersion"] == "2025-06-18");

    CHECK_FALSE(p.Handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
    CHECK(Call(p, "ping")["result"] == Json::object());
    CHECK(Call(p, "nonsense")["error"]["code"] == -32601);
    CHECK(Json::parse(*p.Handle("{oops"))["error"]["code"] == -32700);
}

TEST_CASE("MCP: host commands become tools") {
    FakeHost        host;
    bzmcp::Protocol p([&](const std::string& r) { return host(r); }, "0.2.0");

    const Json tools = Call(p, "tools/list")["result"]["tools"];
    REQUIRE(tools.size() == 2);   // commands.list itself is not a tool
    CHECK(tools[0]["name"] == "part_tree");
    CHECK(tools[0]["annotations"]["readOnlyHint"] == true);
    CHECK(tools[1]["name"] == "part_set_parameter");
    CHECK(tools[1]["annotations"]["readOnlyHint"] == false);

    Json r = Call(p, "tools/call", {{"name", "part_tree"}, {"arguments", {{"a", 1}}}});
    CHECK(r["result"]["isError"] == false);
    CHECK(Json::parse(r["result"]["content"][0]["text"].get<std::string>())["features"].is_array());
    CHECK(Json::parse(host.last) == Json{{"command", "part.tree"}, {"args", {{"a", 1}}}});

    r = Call(p, "tools/call", {{"name", "part_set_parameter"}});
    CHECK(r["result"]["isError"] == true);
    CHECK(r["result"]["content"][0]["text"] == "Нет параметра «x»");
    CHECK(Call(p, "tools/call", {{"name", "no_such"}})["result"]["isError"] == true);
}

TEST_CASE("MCP: a client that calls a tool without listing first") {
    FakeHost        host;
    bzmcp::Protocol p([&](const std::string& r) { return host(r); }, "0.2.0");
    CHECK(Call(p, "tools/call", {{"name", "part_tree"}})["result"]["isError"] == false);
    CHECK(bzmcp::Protocol::ToolName("app.new_part") == "app_new_part");
}

// --- the real module: loaded, served over 127.0.0.1, switched off ------------

#include "Connection.h"
#include "Net.h"
#include "modules/ModuleHost.h"
#include "modules/ModuleManager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>

namespace {

std::string Post(std::uint16_t port, const std::string& token, const std::string& body) {
    const bzmcp::Socket s = bzmcp::Socket::Connect(port);
    if (!s.IsOpen()) return "no connection";
    s.SetTimeout(20000);
    s.WriteAll("POST /mcp HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + token + "\r\nContent-Length: " +
               std::to_string(body.size()) + "\r\n\r\n" + body);
    const auto reply = bzmcp::ReadHttp(s);
    return reply ? reply->startLine + "\n" + reply->body : "no reply";
}

} // namespace

TEST_CASE("MCP module: serves requests through the main thread and shuts down cleanly") {
    namespace fs = std::filesystem;
    using namespace bz::modules;
    const fs::path root = fs::temp_directory_path() / "bz_mcp_module";
    fs::remove_all(root);
    fs::create_directories(root / "mods" / "mcp");
    fs::copy_file(BZ_MCP_MODULE, root / "mods" / "mcp" / fs::path(BZ_MCP_MODULE).filename());
    fs::copy_file(fs::path(BZ_SOURCE_DIR) / "modules" / "mcp" / "module.json", root / "mods" / "mcp" / "module.json");
    // A port of its own, so a BreezeCAD running next to the tests does not matter.
    fs::create_directories(root / "config" / "mcp");
    std::ofstream(root / "config" / "mcp" / "connection.json") << R"({"port": 47399, "token": "test-token-1234567890"})";

    const std::u8string config = (root / "config").generic_u8string();
    SetConfigDir(std::string(config.begin(), config.end()));
    FakeHost host;
    SetCommandHandler([&](std::string_view r) { return host(std::string(r)); });
    SetWakeHandler({});

    ModuleManager mm(root / "config" / "modules.json", HostApi());
    mm.Discover(root / "mods", HostInfo::Detect());
    mm.LoadAll();
    REQUIRE(mm.Find("mcp")->state == ModuleState::Off);   // off until the user agrees
    REQUIRE(mm.TurnOn("mcp"));

    // Requests wait for the main thread: this test plays it, ticking.
    const auto ask = [&](const std::string& token, const std::string& body) {
        auto reply = std::async(std::launch::async, [=] { return Post(47399, token, body); });
        while (reply.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) mm.Tick();
        return reply.get();
    };
    CHECK(ask("wrong", R"({"jsonrpc":"2.0","id":1,"method":"ping"})").rfind("HTTP/1.1 401", 0) == 0);
    const std::string list = ask("test-token-1234567890", R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    CHECK(list.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(list.find("part_set_parameter") != std::string::npos);
    const std::string note = ask("test-token-1234567890", R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    CHECK(note.rfind("HTTP/1.1 202", 0) == 0);

    const auto t0 = std::chrono::steady_clock::now();
    CHECK(mm.TurnOff("mcp"));   // the server thread stops and is joined
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    CHECK(Post(47399, "test-token-1234567890", "{}") == "no connection");

    SetCommandHandler({});
    fs::remove_all(root);
}

TEST_CASE("MCP package: installs from the .bzmod, serves, and is removed") {
    // The file users download: installed as they would, with the bridge inside.
    namespace fs = std::filesystem;
    using namespace bz::modules;
    const fs::path root = fs::temp_directory_path() / "bz_mcp_package";
    fs::remove_all(root);
    fs::create_directories(root / "config" / "mcp");
    std::ofstream(root / "config" / "mcp" / "connection.json") << R"({"port": 47398, "token": "test-token-0987654321"})";
    const std::u8string config = (root / "config").generic_u8string();
    SetConfigDir(std::string(config.begin(), config.end()));
    FakeHost host;
    SetCommandHandler([&](std::string_view r) { return host(std::string(r)); });
    SetWakeHandler({});

    ModuleManager mm(root / "config" / "modules.json", HostApi());
    mm.SetInstallDir(root / "config" / "modules");
    mm.Discover(root / "config" / "modules", HostInfo::Detect());
    const bz::Expected<std::string> id = mm.Install(BZ_MCP_PACKAGE);
    REQUIRE_MESSAGE(id, (id ? "" : id.GetError().message));
    CHECK(*id == "mcp");
    REQUIRE(mm.Find("mcp")->state == ModuleState::Loaded);   // installing is the consent: on at once
    const fs::path folder = root / "config" / "modules" / "mcp";
#if defined(_WIN32)
    CHECK(fs::exists(folder / "bz-mcp-bridge.exe"));
#else
    CHECK(fs::exists(folder / "bz-mcp-bridge"));
    CHECK((fs::status(folder / "bz-mcp-bridge").permissions() & fs::perms::owner_exec) != fs::perms::none);   // Claude starts it
#endif
    CHECK(fs::exists(folder / "README.md"));

    auto reply = std::async(std::launch::async, [] { return Post(47398, "test-token-0987654321", R"({"jsonrpc":"2.0","id":1,"method":"ping"})"); });
    while (reply.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) mm.Tick();
    CHECK(reply.get().rfind("HTTP/1.1 200", 0) == 0);

    REQUIRE(mm.Uninstall("mcp"));
    CHECK_FALSE(fs::exists(folder));
    CHECK(Post(47398, "test-token-0987654321", "{}") == "no connection");
    SetCommandHandler({});
    fs::remove_all(root);
}
