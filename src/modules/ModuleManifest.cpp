#include "modules/ModuleManifest.h"

#include "core/Version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

namespace bz::modules {

using Json = nlohmann::json;

namespace {

bool ValidId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.';
    });
}

/// "hello" -> the usual file name of a library on `os`.
std::string LibraryFileName(const std::string& base, std::string_view os) {
    if (base.find('.') != std::string::npos) return base;   // already a file name
    if (os == "windows") return base + ".dll";
    if (os == "macos") return "lib" + base + ".dylib";
    return "lib" + base + ".so";
}

bool ReadString(const Json& j, const char* key, std::string& out) {
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (!it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

bool ReadUnsigned(const Json& j, const char* key, unsigned& out) {
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (!it->is_number_unsigned()) return false;
    out = it->get<unsigned>();
    return true;
}

} // namespace

int CompareVersions(std::string_view a, std::string_view b) {
    auto next = [](std::string_view& s) {
        unsigned v = 0;
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        (void)ec;
        s.remove_prefix(static_cast<size_t>(end - s.data()));
        if (!s.empty() && s.front() == '.') s.remove_prefix(1);
        else s = {};   // "-beta" and the like end the comparison
        return v;
    };
    while (!a.empty() || !b.empty()) {
        const unsigned x = a.empty() ? 0 : next(a);
        const unsigned y = b.empty() ? 0 : next(b);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

Expected<Manifest> ParseManifest(std::string_view json, std::string_view os) {
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return Error{"Описание модуля повреждено (не JSON)"};

    Manifest m;
    if (!ReadString(root, "id", m.id) || !ValidId(m.id))
        return Error{"В описании модуля нет правильного «id» (латиница, цифры, «-», «_», «.»)"};
    if (!ReadString(root, "name", m.name) || !ReadString(root, "version", m.version) ||
        !ReadString(root, "description", m.description) || !ReadUnsigned(root, "abiVersion", m.abiVersion))
        return Error{std::format("Описание модуля «{}» повреждено", m.id)};
    if (m.name.empty()) m.name = m.id;
    if (const auto on = root.find("enabledByDefault"); on != root.end()) {
        if (!on->is_boolean()) return Error{std::format("Описание модуля «{}»: «enabledByDefault» — не true/false", m.id)};
        m.enabledByDefault = on->get<bool>();
    }
    if (m.abiVersion == 0) return Error{std::format("В описании модуля «{}» нет «abiVersion»", m.id)};

    const auto lib = root.find("library");
    if (lib != root.end() && lib->is_string()) {
        m.library = LibraryFileName(lib->get<std::string>(), os);
    } else if (lib != root.end() && lib->is_object()) {
        if (const auto it = lib->find(std::string(os)); it != lib->end() && it->is_string()) m.library = it->get<std::string>();
    }
    if (m.library.empty() || m.library.find_first_of("/\\") != std::string::npos)
        return Error{std::format("Модуль «{}»: не указан файл библиотеки для этой системы", m.name)};

    if (const auto req = root.find("requirements"); req != root.end()) {
        Requirements& r = m.requirements;
        if (!req->is_object() || !ReadString(*req, "minHostVersion", r.minHostVersion) ||
            !ReadUnsigned(*req, "minMemoryMb", r.minMemoryMb) || !ReadUnsigned(*req, "minCpuCores", r.minCpuCores) ||
            !ReadString(*req, "minOpenGL", r.minOpenGL))
            return Error{std::format("Модуль «{}»: требования записаны неверно", m.name)};
        if (const auto list = req->find("os"); list != req->end()) {
            if (!list->is_array()) return Error{std::format("Модуль «{}»: требования записаны неверно", m.name)};
            for (const Json& v : *list)
                if (v.is_string()) r.os.push_back(v.get<std::string>());
        }
    }
    return m;
}

Expected<Manifest> ReadManifest(const std::filesystem::path& file, std::string_view os) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return Error{"Нет файла описания модуля module.json"};
    std::ostringstream text;
    text << in.rdbuf();
    return ParseManifest(text.str(), os);
}

std::vector<std::string> MissingRequirements(const Requirements& r, const HostInfo& host) {
    std::vector<std::string> missing;
    if (!r.os.empty() && !host.os.empty() && std::find(r.os.begin(), r.os.end(), host.os) == r.os.end())
        missing.push_back(std::format("Не работает на этой системе ({})", host.os));
    if (!r.minHostVersion.empty() && !host.hostVersion.empty() && CompareVersions(host.hostVersion, r.minHostVersion) < 0)
        missing.push_back(std::format("Нужен BreezeCAD {} или новее (сейчас {})", r.minHostVersion, host.hostVersion));
    if (r.minMemoryMb && host.memoryMb && host.memoryMb < r.minMemoryMb)
        missing.push_back(std::format("Нужно не меньше {} МБ памяти (есть {})", r.minMemoryMb, host.memoryMb));
    if (r.minCpuCores && host.cpuCores && host.cpuCores < r.minCpuCores)
        missing.push_back(std::format("Нужно не меньше {} ядер процессора (есть {})", r.minCpuCores, host.cpuCores));
    if (!r.minOpenGL.empty() && !host.openGL.empty() && CompareVersions(host.openGL, r.minOpenGL) < 0)
        missing.push_back(std::format("Нужен OpenGL {} (есть {})", r.minOpenGL, host.openGL));
    return missing;
}

HostInfo HostInfo::Detect() {
    HostInfo h;
#if defined(_WIN32)
    h.os = "windows";
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) h.memoryMb = static_cast<unsigned>(status.ullTotalPhys / (1024 * 1024));
#elif defined(__APPLE__)
    h.os = "macos";
    uint64_t bytes = 0;
    size_t   size  = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) h.memoryMb = static_cast<unsigned>(bytes / (1024 * 1024));
#else
    h.os = "linux";
    const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
        h.memoryMb = static_cast<unsigned>(static_cast<unsigned long long>(pages) * static_cast<unsigned long long>(pageSize) /
                                           (1024ull * 1024ull));
#endif
    h.cpuCores    = std::thread::hardware_concurrency();
    h.hostVersion = Version();
    return h;
}

} // namespace bz::modules
