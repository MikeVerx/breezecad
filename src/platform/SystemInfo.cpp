#include "platform/SystemInfo.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <optional>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace bz::platform {
namespace {

#if defined(_WIN32)

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string RegString(HKEY root, const std::wstring& key, const wchar_t* name) {
    wchar_t buffer[512];
    DWORD size = sizeof buffer;
    if (RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return Narrow(buffer);
}

std::optional<unsigned long long> RegNumber(HKEY root, const std::wstring& key, const wchar_t* name) {
    unsigned long long value = 0;
    DWORD size = sizeof value;
    if (RegGetValueW(root, key.c_str(), name, RRF_RT_REG_QWORD | RRF_RT_REG_DWORD | RRF_RT_REG_BINARY, nullptr, &value, &size) !=
        ERROR_SUCCESS)
        return std::nullopt;
    return value;
}

void Windows(std::vector<SystemFact>& out) {
    const std::wstring nt = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (auto f = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))))
        f(&v);
    std::string product = RegString(HKEY_LOCAL_MACHINE, nt, L"ProductName");
    // Windows 11 still calls itself "Windows 10" in ProductName.
    if (v.dwBuildNumber >= 22000 && product.starts_with("Windows 10")) product.replace(0, 10, "Windows 11");
    const auto ubr = RegNumber(HKEY_LOCAL_MACHINE, nt, L"UBR");
    out.push_back({"os", std::format("{} {} (build {}.{})", product, RegString(HKEY_LOCAL_MACHINE, nt, L"DisplayVersion"), v.dwBuildNumber,
                                     ubr ? *ubr : 0)});

    const std::wstring cpu = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    const auto mhz = RegNumber(HKEY_LOCAL_MACHINE, cpu, L"~MHz");
    out.push_back({"cpu", std::format("{} ({} MHz)", RegString(HKEY_LOCAL_MACHINE, cpu, L"ProcessorNameString"), mhz ? *mhz : 0)});

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof memory;
    if (GlobalMemoryStatusEx(&memory))
        out.push_back({"memory", std::format("{} MB, free {} MB", memory.ullTotalPhys >> 20, memory.ullAvailPhys >> 20)});

    // Graphics cards: name, driver version and date, video memory.
    std::vector<std::string> seen;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof device;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i, device.cb = sizeof device) {
        const std::string name = Narrow(device.DeviceString);
        if (std::find(seen.begin(), seen.end(), name) != seen.end()) continue;
        seen.push_back(name);
        std::wstring key = device.DeviceKey;   // \Registry\Machine\System\CurrentControlSet\Control\Video\{…}\0000
        const std::wstring prefix = L"\\Registry\\Machine\\";
        if (key.size() > prefix.size() && _wcsnicmp(key.c_str(), prefix.c_str(), prefix.size()) == 0) key = key.substr(prefix.size());
        const auto vram = RegNumber(HKEY_LOCAL_MACHINE, key, L"HardwareInformation.qwMemorySize");
        out.push_back({std::format("gpu.{}", seen.size()),
                       std::format("{}; driver {} ({}); video memory {} MB{}", name, RegString(HKEY_LOCAL_MACHINE, key, L"DriverVersion"),
                                   RegString(HKEY_LOCAL_MACHINE, key, L"DriverDate"), vram ? *vram >> 20 : 0,
                                   (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? "; main" : "")});
    }

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power))
        out.push_back({"power", power.BatteryFlag == 128 ? std::string("no battery")
                                : std::format("{}, battery {}%", power.ACLineStatus == 1 ? "on mains" : "on battery",
                                              static_cast<int>(power.BatteryLifePercent))});
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"";
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) out.push_back({"locale", Narrow(locale)});
}

#else

std::string FirstLineWith(const char* file, const std::string& prefix) {
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);)
        if (line.starts_with(prefix)) return line.substr(prefix.size());
    return {};
}

void Unix(std::vector<SystemFact>& out) {
    utsname u{};
    uname(&u);
#if defined(__APPLE__)
    char text[256] = "";
    std::size_t size = sizeof text;
    std::string release;
    if (sysctlbyname("kern.osproductversion", text, &size, nullptr, 0) == 0) release = text;
    out.push_back({"os", std::format("macOS {} ({} {})", release, u.sysname, u.release)});
    size = sizeof text;
    if (sysctlbyname("machdep.cpu.brand_string", text, &size, nullptr, 0) == 0) out.push_back({"cpu", text});
    size = sizeof text;
    if (sysctlbyname("hw.model", text, &size, nullptr, 0) == 0) out.push_back({"model", text});
    std::uint64_t bytes = 0;
    size = sizeof bytes;
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) out.push_back({"memory", std::format("{} MB", bytes >> 20)});
#else
    std::string pretty = FirstLineWith("/etc/os-release", "PRETTY_NAME=");
    if (pretty.size() > 1 && pretty.front() == '"') pretty = pretty.substr(1, pretty.size() - 2);
    out.push_back({"os", std::format("{} ({} {} {})", pretty, u.sysname, u.release, u.machine)});
    std::string cpu = FirstLineWith("/proc/cpuinfo", "model name");
    if (const auto colon = cpu.find(':'); colon != std::string::npos) cpu = cpu.substr(colon + 2);
    out.push_back({"cpu", cpu});
    const long pages = sysconf(_SC_PHYS_PAGES), avail = sysconf(_SC_AVPHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page > 0)
        out.push_back({"memory", std::format("{} MB, free {} MB", (static_cast<unsigned long long>(pages) * static_cast<unsigned long long>(page)) >> 20,
                                             (static_cast<unsigned long long>(avail) * static_cast<unsigned long long>(page)) >> 20)});
#endif
}

#endif

} // namespace

std::vector<SystemFact> DescribeSystem(const std::filesystem::path& disk) {
    std::vector<SystemFact> out;
#if defined(_WIN32)
    Windows(out);
#else
    Unix(out);
#endif
    out.push_back({"cpu.threads", std::to_string(std::thread::hardware_concurrency())});
    std::error_code ec;
    if (const auto space = std::filesystem::space(disk, ec); !ec)
        out.push_back({"disk.free", std::format("{} MB", space.available >> 20)});
    // Screens (GLFW is up by the time reports are written).
    int count = 0;
    if (GLFWmonitor** monitors = glfwGetMonitors(&count))
        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            float sx = 1, sy = 1;
            glfwGetMonitorContentScale(monitors[i], &sx, &sy);
            const char* name = glfwGetMonitorName(monitors[i]);
            out.push_back({std::format("display.{}", i + 1),
                           std::format("{}: {}x{} @ {} Hz, scale {:.0f}%", name ? name : "?", mode ? mode->width : 0, mode ? mode->height : 0,
                                       mode ? mode->refreshRate : 0, sx * 100)});
        }
    out.push_back({"glfw", glfwGetVersionString()});
    return out;
}

} // namespace bz::platform
