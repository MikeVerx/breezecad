#include "Payload.h"

#include "Install.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
extern char** environ;
#endif

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <format>
#include <mutex>

#ifndef BZ_SETUP_CHANNEL
#define BZ_SETUP_CHANNEL "latest"   // or a release tag ("next-build"): set by CI
#endif
#ifndef BZ_SETUP_VERSION
#define BZ_SETUP_VERSION "0"
#endif

namespace bz::setup {
namespace {

constexpr char kTrailer[8] = {'B', 'Z', 'S', 'E', 'T', 'U', 'P', '1'};
#ifdef _WIN32
constexpr const wchar_t* kHost = L"github.com";
#endif
constexpr const char* kRepo = "/MikeVerx/breezecad/releases";

/// This .exe mapped into memory once: the packed files are read from it in place.
struct Self {
    std::span<const std::uint8_t> exe;       ///< without the packed files
    std::span<const std::uint8_t> payload;   ///< the ZIP; empty for the online installer
};

const Self& Mapped() {
    static const Self self = [] {
        Self s;
        const std::uint8_t* bytes = nullptr;
        std::size_t total = 0;
#ifdef _WIN32
        const std::wstring path = SelfPath().wstring();
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return s;
        LARGE_INTEGER size{};
        GetFileSizeEx(file, &size);
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        CloseHandle(file);
        if (!mapping) return s;
        const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);   // the view keeps it
        if (!view) return s;
        bytes = static_cast<const std::uint8_t*>(view);
        total = static_cast<std::size_t>(size.QuadPart);
#else
        const int fd = ::open(SelfPath().c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return s;
        struct stat st{};
        ::fstat(fd, &st);
        void* view = st.st_size > 0 ? ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
        ::close(fd);
        if (view == MAP_FAILED) return s;
        bytes = static_cast<const std::uint8_t*>(view);
        total = static_cast<std::size_t>(st.st_size);
#endif
        s.exe = {bytes, total};
        if (total > 16 && std::memcmp(bytes + total - 16, kTrailer, 8) == 0) {
            std::uint64_t offset = 0;
            std::memcpy(&offset, bytes + total - 8, 8);
            if (offset > 0 && offset < total - 16) {
                s.exe = {bytes, static_cast<std::size_t>(offset)};
                s.payload = {bytes + offset, static_cast<std::size_t>(total - 16 - offset)};
            }
        }
        return s;
    }();
    return self;
}

std::optional<std::vector<std::uint8_t>> PayloadEntry(const std::string& name, std::string& error) {
    const auto payload = Mapped().payload;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, payload.data(), payload.size(), 0)) {
        error = "the packed files are damaged";
        return std::nullopt;
    }
    std::optional<std::vector<std::uint8_t>> out;
    const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
    std::size_t size = 0;
    if (index >= 0)
        if (void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0)) {
            const auto* b = static_cast<const std::uint8_t*>(data);
            out.emplace(b, b + size);
            mz_free(data);
        }
    mz_zip_reader_end(&zip);
    if (!out) error = "not inside the installer: " + name;
    return out;
}

#ifdef _WIN32
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

/// GET https://github.com<path>, following GitHub's redirects to its file storage.
std::optional<std::vector<std::uint8_t>> Download(const std::string& path, std::uint64_t expected, const Progress& progress,
                                                  const std::atomic<bool>& cancel, std::string& error) {
    const std::wstring agent = std::format(L"BreezeCAD-Setup/{}", Wide(BZ_SETUP_VERSION));
    // Windows 8.1+: the system's proxy settings, as the browser uses them.
    Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        error = std::format("WinHttpOpen {}", GetLastError());
        return std::nullopt;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /* TLS 1.3 */;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 60000);
    Handle connection(WinHttpConnect(session.h, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    const std::wstring object = Wide(path);
    Handle request(connection.h ? WinHttpOpenRequest(connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr);
    if (!request.h || !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = std::format("no connection to GitHub ({})", GetLastError());
        return std::nullopt;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = std::format("GitHub answered {} for {}", status, path);
        return std::nullopt;
    }
    std::uint64_t total = expected;
    wchar_t length[32] = {};
    size = sizeof(length);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &size, WINHTTP_NO_HEADER_INDEX))
        total = std::wcstoull(length, nullptr, 10);
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(total));
    std::vector<std::uint8_t> chunk(256 * 1024);
    for (;;) {
        if (cancel) {
            error = "cancelled";
            return std::nullopt;
        }
        DWORD got = 0;
        if (!WinHttpReadData(request.h, chunk.data(), static_cast<DWORD>(chunk.size()), &got)) {
            error = std::format("the download broke off ({})", GetLastError());
            return std::nullopt;
        }
        if (got == 0) break;
        out.insert(out.end(), chunk.begin(), chunk.begin() + got);
        if (progress) progress(out.size(), total ? total : out.size());
    }
    return out;
}

#else
/// GET https://github.com<path> with curl (every desktop Linux has it, with
/// the system's certificates and proxy settings); the file grows in a
/// temporary place, its size is the progress.
std::optional<std::vector<std::uint8_t>> Download(const std::string& path, std::uint64_t expected, const Progress& progress,
                                                  const std::atomic<bool>& cancel, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    static std::atomic<int> counter{0};
    const fs::path temp = fs::temp_directory_path(ec) / std::format("bz-setup-{}-{}.part", ::getpid(), counter++);
    const std::string url = std::string("https://github.com") + path;
    const std::string agent = std::format("BreezeCAD-Setup/{}", BZ_SETUP_VERSION);
    std::vector<std::string> argv = {"curl", "-fsSL", "--connect-timeout", "15", "-A", agent, "-o", temp.string(), url};
    std::vector<char*> args;
    for (std::string& a : argv) args.push_back(a.data());
    args.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, "curl", nullptr, nullptr, args.data(), environ) != 0) {
        error = "curl is not installed";
        return std::nullopt;
    }
    int status = 0;
    for (;;) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (cancel) {
            ::kill(pid, SIGTERM);
            ::waitpid(pid, &status, 0);
            fs::remove(temp, ec);
            error = "cancelled";
            return std::nullopt;
        }
        if (progress) {
            const auto now = fs::file_size(temp, ec);
            if (!ec) progress(now, expected ? expected : now);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (code != 0) {
        fs::remove(temp, ec);
        error = code == 22 ? std::format("GitHub has no {}", path) : std::format("no connection to GitHub (curl {})", code);
        return std::nullopt;
    }
    std::ifstream in(temp, std::ios::binary);
    std::vector<std::uint8_t> out{std::istreambuf_iterator<char>(in), {}};
    in.close();
    fs::remove(temp, ec);
    if (progress) progress(out.size(), out.size());
    return out;
}

#endif

std::string ReleasePath(const std::string& tag, const std::string& file) {
    if (tag.empty() || tag == "latest") return std::format("{}/latest/download/{}", kRepo, file);
    return std::format("{}/download/{}/{}", kRepo, tag, file);
}

Component ReadComponent(const nlohmann::json& j) {
    Component c;
    c.id = j.value("id", std::string{});
    c.file = j.value("file", std::string{});
    c.name = j.value("name", std::string{});
    c.description = j.value("description", std::string{});
    c.size = j.value("size", std::uint64_t{0});
    c.unpacked = j.value("unpacked", std::uint64_t{0});
    c.sha256 = j.value("sha256", std::string{});
    return c;
}

} // namespace

const Component* Manifest::Language(const std::string& code) const {
    for (const Component& c : languages)
        if (c.id == code) return &c;
    return nullptr;
}
const Component* Manifest::Module(const std::string& id) const {
    for (const Component& c : modules)
        if (c.id == id) return &c;
    return nullptr;
}

bool Offline() { return !Mapped().payload.empty(); }
std::span<const std::uint8_t> InstallerExe() { return Mapped().exe; }

std::optional<Manifest> LoadManifest(std::string& error) {
    std::optional<std::vector<std::uint8_t>> bytes;
    if (Offline()) bytes = PayloadEntry("setup-manifest.json", error);
    else {
        const std::atomic<bool> never{false};
#ifdef _WIN32
        bytes = Download(ReleasePath(BZ_SETUP_CHANNEL, "setup-manifest.json"), 0, {}, never, error);
#else
        bytes = Download(ReleasePath(BZ_SETUP_CHANNEL, "setup-manifest-linux.json"), 0, {}, never, error);
#endif
    }
    if (!bytes) return std::nullopt;
    try {
        const auto j = nlohmann::json::parse(bytes->begin(), bytes->end());
        Manifest m;
        m.version = j.at("version").get<std::string>();
        m.tag = j.value("tag", std::string(BZ_SETUP_CHANNEL));
        m.program = ReadComponent(j.at("program"));
        m.program.id = "program";
        for (const auto& c : j.value("modules", nlohmann::json::array())) m.modules.push_back(ReadComponent(c));
        for (const auto& c : j.value("languages", nlohmann::json::array())) m.languages.push_back(ReadComponent(c));
        if (m.program.file.empty()) throw std::runtime_error("no program file");
        return m;
    } catch (const std::exception& e) {
        error = std::string("the manifest is damaged: ") + e.what();
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> Fetch(const Manifest& m, const Component& c, const Progress& progress,
                                               const std::atomic<bool>& cancel, std::string& error) {
    std::optional<std::vector<std::uint8_t>> bytes;
    if (Offline()) {
        bytes = PayloadEntry(c.file, error);
        if (bytes && progress) progress(bytes->size(), bytes->size());
    } else {
        bytes = Download(ReleasePath(m.tag, c.file), c.size, progress, cancel, error);
    }
    if (bytes && !c.sha256.empty() && Sha256(*bytes) != c.sha256) {
        error = "the file came damaged: " + c.file;
        return std::nullopt;
    }
    return bytes;
}

#ifdef _WIN32
std::string Sha256(std::span<const std::uint8_t> bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::uint8_t digest[32] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    for (std::size_t at = 0; ok && at < bytes.size();) {
        const ULONG n = static_cast<ULONG>(std::min<std::size_t>(bytes.size() - at, 1u << 30));
        ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes.data() + at), n, 0) == 0;
        at += n;
    }
    ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    std::string out;
    for (std::uint8_t b : digest) out += std::format("{:02x}", b);
    return out;
}

#else
std::string Sha256(std::span<const std::uint8_t> bytes) {
    // FIPS 180-4, as small as it goes: the downloads are checked once each.
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    const auto block = [&](const std::uint8_t* p) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = std::uint32_t(p[4 * i]) << 24 | std::uint32_t(p[4 * i + 1]) << 16 | std::uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    };
    std::size_t at = 0;
    for (; at + 64 <= bytes.size(); at += 64) block(bytes.data() + at);
    std::uint8_t tail[128] = {};
    const std::size_t rest = bytes.size() - at;
    std::memcpy(tail, bytes.data() + at, rest);
    tail[rest] = 0x80;
    const std::size_t len = rest < 56 ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(bytes.size()) * 8;
    for (int i = 0; i < 8; ++i) tail[len - 1 - i] = static_cast<std::uint8_t>(bits >> (8 * i));
    block(tail);
    if (len == 128) block(tail + 64);
    std::string out;
    for (std::uint32_t v : h) out += std::format("{:08x}", v);
    return out;
}

#endif

} // namespace bz::setup
