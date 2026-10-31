#include "Payload.h"

#include "Install.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

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
constexpr const wchar_t* kHost = L"github.com";
constexpr const char* kRepo = "/MikeVerx/breezecad/releases";

/// This .exe mapped into memory once: the packed files are read from it in place.
struct Self {
    std::span<const std::uint8_t> exe;       ///< without the packed files
    std::span<const std::uint8_t> payload;   ///< the ZIP; empty for the online installer
};

const Self& Mapped() {
    static const Self self = [] {
        Self s;
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
        const auto* bytes = static_cast<const std::uint8_t*>(view);
        const auto total = static_cast<std::size_t>(size.QuadPart);
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
        bytes = Download(ReleasePath(BZ_SETUP_CHANNEL, "setup-manifest.json"), 0, {}, never, error);
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

} // namespace bz::setup
