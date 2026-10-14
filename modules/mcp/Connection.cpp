#include "Connection.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

namespace bzmcp {

namespace {

std::string NewToken() {
    std::random_device              rd;
    std::uniform_int_distribution<> hex(0, 15);
    std::string                     out;
    for (int i = 0; i < 40; ++i) out += "0123456789abcdef"[hex(rd)];
    return out;
}

} // namespace

std::filesystem::path ConnectionFile(const std::filesystem::path& configDir) { return configDir / "mcp" / "connection.json"; }

std::optional<Connection> ReadConnection(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    const std::string    text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    Connection c;
    if (j.contains("port") && j["port"].is_number_unsigned() && j["port"].get<unsigned>() > 0 && j["port"].get<unsigned>() < 65536)
        c.port = static_cast<std::uint16_t>(j["port"].get<unsigned>());
    if (j.contains("token") && j["token"].is_string()) c.token = j["token"].get<std::string>();
    return c;
}

std::optional<Connection> PrepareConnection(const std::filesystem::path& file, std::string* error) {
    Connection c = ReadConnection(file).value_or(Connection{});
    if (c.token.size() < 16) c.token = NewToken();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const nlohmann::json j = {{"url", c.Url()}, {"port", c.port}, {"token", c.token}};
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << j.dump(1, '\t');
    if (!out) {
        if (error) *error = "не удалось записать " + file.string();
        return std::nullopt;
    }
    return c;
}

} // namespace bzmcp
