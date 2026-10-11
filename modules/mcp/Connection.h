#pragma once
// Where the MCP server listens and the secret a client has to show.
// Kept in <BreezeCAD settings>/mcp/connection.json:
//   {"url": "http://127.0.0.1:47321/mcp", "port": 47321, "token": "…"}
// The module writes it on start (keeping a port and token already there),
// the bridge reads it. The token keeps other programs and web pages that
// reach 127.0.0.1 from driving the CAD.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace bzmcp {

inline constexpr std::uint16_t kDefaultPort = 47321;

struct Connection {
    std::uint16_t port = kDefaultPort;
    std::string   token;

    std::string Url() const { return "http://127.0.0.1:" + std::to_string(port) + "/mcp"; }
};

/// <configDir>/mcp/connection.json
std::filesystem::path ConnectionFile(const std::filesystem::path& configDir);

/// The file's content; nullopt when it is missing or broken.
std::optional<Connection> ReadConnection(const std::filesystem::path& file);

/// Reads the file, fills in what is missing (a new random token), writes it back.
std::optional<Connection> PrepareConnection(const std::filesystem::path& file, std::string* error);

} // namespace bzmcp
