// bz-mcp-bridge: MCP over stdio for clients that start a local program
// (Claude Desktop). Each JSON-RPC line from stdin goes to the BreezeCAD MCP
// module over HTTP (address and token from <settings>/mcp/connection.json),
// the reply goes to stdout as one line.
//
// When BreezeCAD is not running (or the module is off) the bridge answers
// itself: initialize works, tools say how to start the program, so the
// client shows a clear message instead of a dead server.
//
//   claude_desktop_config.json:
//   {"mcpServers": {"breezecad": {"command": "C:/BreezeCAD/modules/mcp/bz-mcp-bridge.exe"}}}

#include "Connection.h"
#include "Net.h"
#include "Protocol.h"
#include "platform/Paths.h"

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

constexpr const char* kNotRunning =
    "BreezeCAD не запущен или модуль MCP выключен. Запустите BreezeCAD и включите модуль: "
    "Настройки → Модули → «MCP-сервер для Claude».";

struct Reply {
    int         status = 0;   ///< 0: nobody listens
    std::string body;
};

Reply Post(const bzmcp::Connection& c, const std::string& body) {
    const bzmcp::Socket s = bzmcp::Socket::Connect(c.port);
    if (!s.IsOpen()) return {};
    s.SetTimeout(200000);   // the module itself waits up to three minutes for the program
    const std::string request = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(c.port) +
                                "\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\n"
                                "Authorization: Bearer " + c.token + "\r\nContent-Length: " + std::to_string(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body;
    if (!s.WriteAll(request)) return {};
    const auto msg = bzmcp::ReadHttp(s);
    if (!msg) return {};
    Reply r;
    const auto sp = msg->startLine.find(' ');
    try {
        r.status = sp == std::string::npos ? 0 : std::stoi(msg->startLine.substr(sp + 1, 3));
    } catch (...) {
        r.status = 0;
    }
    r.body = msg->body;
    return r;
}

} // namespace

int main() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::ios::sync_with_stdio(false);
    bzmcp::NetInit();
    const std::filesystem::path file = bzmcp::ConnectionFile(bz::platform::ConfigDir());
    // Answers when the program is not there (see the top of the file).
    bzmcp::Protocol offline([](const std::string&) { return std::string(R"({"ok":false,"error":")") + kNotRunning + "\"}"; },
                            "offline");

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        std::optional<std::string> out;
        const auto connection = bzmcp::ReadConnection(file);
        const Reply r = connection ? Post(*connection, line) : Reply{};
        if (r.status == 200) {
            out = r.body;
        } else if (r.status == 202) {
            out.reset();   // a notification: nothing to say
        } else if (r.status == 0) {
            out = offline.Handle(line);
        } else {
            std::cerr << "bz-mcp-bridge: HTTP " << r.status << ": " << r.body << '\n';
            const nlohmann::json m = nlohmann::json::parse(line, nullptr, false);
            if (m.is_object() && m.contains("id"))
                out = nlohmann::json{{"jsonrpc", "2.0"}, {"id", m["id"]},
                                     {"error", {{"code", -32000}, {"message", "BreezeCAD: HTTP " + std::to_string(r.status) + " " + r.body}}}}
                          .dump();
        }
        if (out) {
            std::cout << *out << '\n';
            std::cout.flush();
        }
    }
    return 0;
}
