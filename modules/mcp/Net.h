#pragma once
// Just enough TCP and HTTP/1.1 for a local MCP server and its stdio bridge:
// 127.0.0.1 only, one request per connection, bodies with Content-Length.

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace bzmcp {

/// Starts the socket library once (WSAStartup on Windows).
bool NetInit();

class Socket {
public:
    Socket() = default;
    explicit Socket(std::intptr_t handle) : myHandle(handle) {}
    ~Socket() { Close(); }
    Socket(Socket&& o) noexcept : myHandle(o.myHandle) { o.myHandle = -1; }
    Socket& operator=(Socket&& o) noexcept;
    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    bool IsOpen() const { return myHandle != -1; }
    void Close();

    /// Listening on 127.0.0.1:port; empty when the port is taken.
    static Socket Listen(std::uint16_t port, std::string* error);
    /// Connected to 127.0.0.1:port; empty when nothing listens there.
    static Socket Connect(std::uint16_t port);

    /// Waits up to `timeoutMs` for a connection; empty on timeout.
    Socket Accept(int timeoutMs) const;
    /// Gives up reading or writing after `ms` without progress.
    void SetTimeout(int ms) const;

    bool WriteAll(const std::string& data) const;
    /// Some bytes, appended; false when the peer closed or on an error.
    bool ReadSome(std::string& out) const;

private:
    std::intptr_t myHandle = -1;
};

struct HttpMessage {
    std::string                        startLine;   ///< "POST /mcp HTTP/1.1" or "HTTP/1.1 200 OK"
    std::map<std::string, std::string> headers;     ///< names in lower case
    std::string                        body;

    std::string Header(const std::string& lowerName) const {
        const auto it = headers.find(lowerName);
        return it == headers.end() ? std::string() : it->second;
    }
};

/// Reads one message (headers + Content-Length body); nullopt when broken
/// or larger than `maxBytes`.
std::optional<HttpMessage> ReadHttp(const Socket& s, std::size_t maxBytes = 16u << 20);

/// "HTTP/1.1 200 OK" + headers + body, Connection: close.
std::string HttpResponse(int status, const std::string& reason, const std::string& contentType, const std::string& body);

} // namespace bzmcp
