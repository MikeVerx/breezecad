#include "Net.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using NativeSocket = SOCKET;
using IoSize       = int;   // send/recv take int lengths here
#define BZ_INVALID INVALID_SOCKET
#define BZ_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using NativeSocket = int;
using IoSize       = std::size_t;
#define BZ_INVALID (-1)
#define BZ_CLOSE ::close
#endif

namespace bzmcp {

namespace {

NativeSocket Native(std::intptr_t h) { return static_cast<NativeSocket>(h); }

sockaddr_in Loopback(std::uint16_t port) {
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // never other computers
    return a;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    const auto e = s.find_last_not_of(" \t\r");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

} // namespace

bool NetInit() {
#if defined(_WIN32)
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        Close();
        myHandle   = o.myHandle;
        o.myHandle = -1;
    }
    return *this;
}

void Socket::Close() {
    if (myHandle != -1) BZ_CLOSE(Native(myHandle));
    myHandle = -1;
}

Socket Socket::Listen(std::uint16_t port, std::string* error) {
    NetInit();
    const NativeSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BZ_INVALID) {
        if (error) *error = "не удалось создать сокет";
        return {};
    }
    Socket      sock(static_cast<std::intptr_t>(s));
#if !defined(_WIN32)
    int yes = 1;   // restart at once after closing (TIME_WAIT); Windows' SO_REUSEADDR would share the port
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
#endif
    sockaddr_in addr = Loopback(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(s, 8) != 0) {
        if (error) *error = "порт " + std::to_string(port) + " занят другой программой";
        return {};
    }
    return sock;
}

Socket Socket::Connect(std::uint16_t port) {
    NetInit();
    const NativeSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BZ_INVALID) return {};
    Socket      sock(static_cast<std::intptr_t>(s));
    sockaddr_in addr = Loopback(port);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return {};
    return sock;
}

Socket Socket::Accept(int timeoutMs) const {
    // select(): the same on every system (WSAPoll needs a newer Windows SDK setting).
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(Native(myHandle), &ready);
    timeval wait{};
    wait.tv_sec  = timeoutMs / 1000;
    wait.tv_usec = (timeoutMs % 1000) * 1000;
    if (::select(static_cast<int>(Native(myHandle)) + 1, &ready, nullptr, nullptr, &wait) <= 0) return {};
    const NativeSocket c = ::accept(Native(myHandle), nullptr, nullptr);
    if (c == BZ_INVALID) return {};
    return Socket(static_cast<std::intptr_t>(c));
}

void Socket::SetTimeout(int ms) const {
#if defined(_WIN32)
    const DWORD t = static_cast<DWORD>(ms);
#else
    timeval t{};
    t.tv_sec  = ms / 1000;
    t.tv_usec = (ms % 1000) * 1000;
#endif
    setsockopt(Native(myHandle), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    setsockopt(Native(myHandle), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
}

bool Socket::WriteAll(const std::string& data) const {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk = static_cast<IoSize>(std::min<std::size_t>(data.size() - sent, 1u << 20));
#if defined(MSG_NOSIGNAL)
        const auto n = ::send(Native(myHandle), data.data() + sent, chunk, MSG_NOSIGNAL);
#else
        const auto n = ::send(Native(myHandle), data.data() + sent, chunk, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool Socket::ReadSome(std::string& out) const {
    char       buf[16384];
    const auto n = ::recv(Native(myHandle), buf, static_cast<IoSize>(sizeof buf), 0);
    if (n <= 0) return false;
    out.append(buf, static_cast<std::size_t>(n));
    return true;
}

std::optional<HttpMessage> ReadHttp(const Socket& s, std::size_t maxBytes) {
    std::string data;
    std::size_t headerEnd = std::string::npos;
    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() > 64 * 1024 || !s.ReadSome(data)) return std::nullopt;
    }
    HttpMessage msg;
    std::size_t pos  = 0;
    bool        first = true;
    while (pos < headerEnd) {
        std::size_t       eol  = data.find("\r\n", pos);
        const std::string line = data.substr(pos, eol - pos);
        pos                    = eol + 2;
        if (first) {
            msg.startLine = line;
            first         = false;
            continue;
        }
        const auto colon = line.find(':');
        if (colon != std::string::npos) msg.headers[Lower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
    }
    std::size_t length = 0;
    if (const std::string cl = msg.Header("content-length"); !cl.empty()) {
        try {
            length = std::stoul(cl);
        } catch (...) {
            return std::nullopt;
        }
    }
    if (length > maxBytes) return std::nullopt;
    msg.body = data.substr(headerEnd + 4);
    while (msg.body.size() < length)
        if (!s.ReadSome(msg.body)) return std::nullopt;
    msg.body.resize(length);
    return msg;
}

std::string HttpResponse(int status, const std::string& reason, const std::string& contentType, const std::string& body) {
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    if (!contentType.empty()) out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
    return out + body;
}

} // namespace bzmcp
