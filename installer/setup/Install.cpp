#include "Install.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace bz::setup {
namespace {

namespace fs = std::filesystem;

constexpr const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BreezeCAD";
constexpr const wchar_t* kExe = L"breezecad.exe";
constexpr const wchar_t* kMarker = L"installed.json";       // the program reads it: an installed copy
constexpr const wchar_t* kFileList = L"installed-files.txt";
constexpr const wchar_t* kThumbs = L"shell\\bz_thumbs.dll";

struct FileType {
    const wchar_t* ext;
    const wchar_t* progId;
    const wchar_t* icon;   ///< in <dir>\shell
};
constexpr FileType kTypes[] = {
    {L".bzc", L"BreezeCAD.Part", L"part.ico"},
    {L".bza", L"BreezeCAD.Assembly", L"assembly.ico"},
    {L".bzmod", L"BreezeCAD.Module", L"module.ico"},
    {L".bzlib", L"BreezeCAD.Library", L"library.ico"},
    {L".bzp", L"BreezeCAD.Project", L"project.ico"},
};

fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw))) out = raw;
    CoTaskMemFree(raw);
    return out;
}

bool SetString(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    return RegSetKeyValueW(root, key.c_str(), name, REG_SZ, value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ==
           ERROR_SUCCESS;
}
bool SetDword(HKEY root, const std::wstring& key, const wchar_t* name, DWORD value) {
    return RegSetKeyValueW(root, key.c_str(), name, REG_DWORD, &value, sizeof(value)) == ERROR_SUCCESS;
}
std::wstring GetString(HKEY root, const std::wstring& key, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t)) return {};
    std::wstring out(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, out.data(), &size) != ERROR_SUCCESS) return {};
    out.resize(wcslen(out.c_str()));
    return out;
}

std::wstring Quoted(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

std::wstring LastError(const wchar_t* what) {
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring out = std::wstring(what) + L": " + (text ? text : std::to_wstring(code).c_str());
    LocalFree(text);
    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' ')) out.pop_back();
    return out;
}

/// Writes a whole file; a file that is in use (the previews library inside
/// Explorer, the program itself) is renamed aside first: Windows lets a
/// loaded file be renamed, just not replaced.
bool WriteFile(const fs::path& file, std::span<const std::uint8_t> bytes, std::wstring& error) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const BOOL ok = bytes.empty() || ::WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(h);
            if (ok && written == bytes.size()) return true;
            error = LastError(file.c_str());
            return false;
        }
        const DWORD why = GetLastError();
        if (attempt == 0 && (why == ERROR_SHARING_VIOLATION || why == ERROR_ACCESS_DENIED || why == ERROR_USER_MAPPED_FILE)) {
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            fs::path aside = file;
            aside += std::format(L".old-{}", tick);
            if (MoveFileExW(file.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                MoveFileExW(aside.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);   // works only elevated; harmless otherwise
                continue;
            }
        }
        SetLastError(why);
        error = LastError(file.c_str());
        return false;
    }
    return false;
}

/// Leftovers renamed aside by an earlier update, deleted once nothing holds them.
void SweepAside(const fs::path& dir) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->path().filename().wstring().find(L".old-") != std::wstring::npos) fs::remove(it->path(), ec);
}

std::vector<std::string> ReadList(const fs::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);)
        if (!line.empty()) out.push_back(line);
    return out;
}

bool SafeRelative(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.front() == '\\' || name.find(':') != std::string::npos) return false;
    std::stringstream parts(name);
    for (std::string part; std::getline(parts, part, '/');)
        if (part == ".." || part.find('\\') != std::string::npos) return false;
    return true;
}

bool ReservedName(std::wstring part) {
    if (const auto dot = part.find(L'.'); dot != std::wstring::npos) part.resize(dot);
    for (auto& c : part) c = static_cast<wchar_t>(std::towupper(c));
    static const std::set<std::wstring> reserved = {L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4", L"COM5",
                                                    L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5",
                                                    L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
    return reserved.contains(part);
}

/// Whether the person may create files in `dir` (or the nearest folder that exists).
bool CanWrite(fs::path dir) {
    std::error_code ec;
    while (!dir.empty() && !fs::is_directory(dir, ec)) {
        const fs::path up = dir.parent_path();
        if (up == dir) return false;
        dir = up;
    }
    const fs::path probe = dir / std::format(L"bz-setup-probe-{}.tmp", GetCurrentProcessId());
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

struct Com {
    bool ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    ~Com() {
        if (ok) CoUninitialize();
    }
};

bool Shortcut(const fs::path& link, const fs::path& target, const std::wstring& description, std::wstring& error) {
    Com com;
    IShellLinkW* shell = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell)))) {
        error = L"IShellLink";
        return false;
    }
    shell->SetPath(target.c_str());
    shell->SetWorkingDirectory(target.parent_path().c_str());
    shell->SetDescription(description.c_str());
    shell->SetIconLocation(target.c_str(), 0);
    IPersistFile* file = nullptr;
    bool ok = SUCCEEDED(shell->QueryInterface(IID_PPV_ARGS(&file))) && SUCCEEDED(file->Save(link.c_str(), TRUE));
    if (file) file->Release();
    shell->Release();
    if (!ok) error = link.wstring();
    return ok;
}

using DllEntry = HRESULT(WINAPI*)();
bool CallThumbs(const fs::path& dll, const char* entry) {
    HMODULE m = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) return false;
    auto call = reinterpret_cast<DllEntry>(reinterpret_cast<void*>(GetProcAddress(m, entry)));
    const bool ok = call && SUCCEEDED(call());
    FreeLibrary(m);
    return ok;
}

} // namespace

std::wstring Wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}
std::string Utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::optional<Installed> FindInstalled() {
    const fs::path dir = GetString(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation");
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir / kExe, ec)) return std::nullopt;
    return Installed{dir, Utf8(GetString(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion"))};
}

fs::path DefaultDir() {
    fs::path base = KnownFolder(FOLDERID_UserProgramFiles);
    if (base.empty()) base = KnownFolder(FOLDERID_LocalAppData) / L"Programs";
    return base / L"BreezeCAD";
}

fs::path ConfigDir() { return KnownFolder(FOLDERID_RoamingAppData) / L"BreezeCAD"; }

PathCheck CheckPath(const std::wstring& raw, std::uint64_t needBytes) {
    PathCheck out;
    std::wstring text = raw;
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    while (!text.empty() && std::iswspace(text.front())) text.erase(text.begin());
    if (text.empty()) return out;
    for (auto& c : text)
        if (c == L'/') c = L'\\';
    if (text.size() < 3 || !std::iswalpha(text[0]) || text[1] != L':' || text[2] != L'\\') {
        out.state = PathState::NotAbsolute;
        return out;
    }
    out.drive = static_cast<wchar_t>(std::towupper(text[0]));
    if (text.find_first_not_of(L'\\', 3) == std::wstring::npos) {
        out.state = PathState::DriveRoot;
        return out;
    }
    for (std::size_t i = 3; i < text.size(); ++i)
        if (wcschr(L"<>:\"|?*", text[i]) || text[i] < 32) {
            out.state = PathState::BadCharacters;
            return out;
        }
    std::wstringstream parts(text.substr(3));
    for (std::wstring part; std::getline(parts, part, L'\\');)
        if (!part.empty() && (part.back() == L'.' || part.back() == L' ' || ReservedName(part))) {
            out.state = PathState::BadCharacters;
            return out;
        }
    const std::wstring root = std::wstring(1, out.drive) + L":\\";
    const UINT type = GetDriveTypeW(root.c_str());
    if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) {
        out.state = PathState::NoDrive;
        return out;
    }
    const fs::path path = fs::path(text).lexically_normal();
    wchar_t windows[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windows, MAX_PATH)) {
        std::wstring a = path.wstring() + L"\\", b = std::wstring(windows) + L"\\";
        for (auto& c : a) c = static_cast<wchar_t>(std::towlower(c));
        for (auto& c : b) c = static_cast<wchar_t>(std::towlower(c));
        if (a.starts_with(b)) {
            out.state = PathState::SystemFolder;
            return out;
        }
    }
    std::error_code ec;
    if (fs::exists(path, ec)) {
        // Ours (an earlier BreezeCAD) or empty: fine. Anything else is someone's.
        const bool ours = fs::exists(path / kExe, ec) || fs::exists(path / kMarker, ec);
        const bool empty = fs::is_directory(path, ec) && fs::directory_iterator(path, ec) == fs::directory_iterator();
        if (!fs::is_directory(path, ec) || (!ours && !empty)) {
            out.state = PathState::Foreign;
            return out;
        }
    }
    ULARGE_INTEGER free{};
    if (GetDiskFreeSpaceExW(root.c_str(), &free, nullptr, nullptr)) out.freeBytes = free.QuadPart;
    if (out.freeBytes && out.freeBytes < needBytes) {
        out.state = PathState::NoSpace;
        return out;
    }
    out.state = CanWrite(path) ? PathState::Ok : PathState::NeedsAdmin;
    return out;
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION e{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &e, sizeof(e), &size) && e.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

std::optional<unsigned long> RunElevated(const std::wstring& arguments) {
    const fs::path self = SelfPath();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = self.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) return std::nullopt;
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return code;
}

bool ProgramRunning(const fs::path& dir) {
    const fs::path exe = dir / kExe;
    HANDLE h = CreateFileW(exe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return false;
    }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

bool Unzip(std::span<const std::uint8_t> bytes, const fs::path& dir, bool stripTop, const Progress& progress,
           std::vector<std::string>* written, std::wstring& error) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        error = L"ZIP";
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    // The common top folder ("BreezeCAD/") left out, when asked and when there is one.
    std::string top;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        total += st.m_uncomp_size;
        std::string name = st.m_filename;
        const auto slash = name.find('/');
        const std::string first = slash == std::string::npos ? std::string{} : name.substr(0, slash + 1);
        if (i == 0) top = first;
        else if (first != top) top.clear();
    }
    if (!stripTop) top.clear();

    std::error_code ec;
    fs::create_directories(dir, ec);
    std::uint64_t done = 0;
    bool ok = true;
    for (mz_uint i = 0; ok && i < count; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        for (auto& c : name)
            if (c == '\\') c = '/';
        if (!top.empty() && name.starts_with(top)) name.erase(0, top.size());
        if (name.empty()) continue;
        if (!SafeRelative(name)) {
            error = L"ZIP: " + Wide(name);
            ok = false;
            break;
        }
        const fs::path target = dir / fs::path(Wide(name)).make_preferred();
        if (st.m_is_directory) {
            fs::create_directories(target, ec);
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data && st.m_uncomp_size) {
            error = L"ZIP: " + Wide(name);
            ok = false;
            break;
        }
        ok = WriteFile(target, {static_cast<const std::uint8_t*>(data), size}, error);
        mz_free(data);
        if (ok && written) written->push_back(name);
        done += st.m_uncomp_size;
        if (progress) progress(done, total);
    }
    mz_zip_reader_end(&zip);
    return ok;
}

void ForgetStaleFiles(const fs::path& dir, const std::vector<std::string>& now) {
    const std::set<std::string> keep(now.begin(), now.end());
    std::error_code ec;
    for (const std::string& old : ReadList(dir / kFileList))
        if (!keep.contains(old) && SafeRelative(old)) fs::remove(dir / fs::path(Wide(old)).make_preferred(), ec);
    std::ofstream out(dir / kFileList, std::ios::trunc);
    for (const std::string& f : now) out << f << '\n';
    SweepAside(dir);
}

bool PlaceModule(std::span<const std::uint8_t> package, const std::string& fileName, std::wstring& error) {
    // The program installs the packages it finds in its modules folder on start (Application::StartModules).
    const fs::path dir = ConfigDir() / L"modules";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return WriteFile(dir / Wide(fileName), package, error);
}

bool PlaceLanguage(std::span<const std::uint8_t> pack, std::wstring& error) {
    return Unzip(pack, ConfigDir() / L"lang", true, {}, nullptr, error);
}

void SetProgramLanguage(const std::string& code) {
    const fs::path dir = ConfigDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / L"settings.json";
    nlohmann::json j = nlohmann::json::object();
    if (std::ifstream in(file); in) {
        try {
            in >> j;
        } catch (...) {
            return;   // a damaged file is the program's to deal with (it keeps a backup)
        }
        if (!j.is_object()) return;
    }
    j["language"] = code;
    std::ofstream out(file, std::ios::trunc);
    out << j.dump(1, '\t');
}

bool FinishFolder(const fs::path& dir, const std::string& version, std::span<const std::uint8_t> installerExe, std::wstring& error) {
    if (!WriteFile(dir / L"uninstall.exe", installerExe, error)) return false;
    // The marker: the program puts its modules and languages into the profile.
    nlohmann::json marker = {{"version", version}, {"by", "BreezeCAD Setup"}};
    const std::string text = marker.dump(1, '\t');
    return WriteFile(dir / kMarker, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}, error);
}

bool Register(const Choices& c, const std::string& version, std::uint64_t sizeBytes, const TypeNames& names, std::wstring& error) {
    const fs::path exe = c.dir / kExe;
    const fs::path uninstaller = c.dir / L"uninstall.exe";

    // Apps & features.
    const std::wstring key = kUninstallKey;
    bool ok = SetString(HKEY_CURRENT_USER, key, L"DisplayName", L"BreezeCAD") &&
              SetString(HKEY_CURRENT_USER, key, L"DisplayVersion", Wide(version)) &&
              SetString(HKEY_CURRENT_USER, key, L"Publisher", L"BreezeCAD") &&
              SetString(HKEY_CURRENT_USER, key, L"DisplayIcon", exe.wstring() + L",0") &&
              SetString(HKEY_CURRENT_USER, key, L"InstallLocation", c.dir.wstring()) &&
              SetString(HKEY_CURRENT_USER, key, L"UninstallString", Quoted(uninstaller) + L" --uninstall") &&
              SetString(HKEY_CURRENT_USER, key, L"QuietUninstallString", Quoted(uninstaller) + L" --uninstall --quiet") &&
              SetString(HKEY_CURRENT_USER, key, L"ModifyPath", Quoted(uninstaller)) &&
              SetString(HKEY_CURRENT_USER, key, L"URLInfoAbout", L"https://breezecad.ru") &&
              SetString(HKEY_CURRENT_USER, key, L"HelpLink", L"https://github.com/MikeVerx/breezecad/issues") &&
              SetDword(HKEY_CURRENT_USER, key, L"NoRepair", 1) &&
              SetDword(HKEY_CURRENT_USER, key, L"EstimatedSize", static_cast<DWORD>(sizeBytes / 1024));
    if (!ok) {
        error = L"HKCU\\" + key;
        return false;
    }

    // File types: every type is known to "Open with"; the chosen ones open in BreezeCAD by default.
    const std::wstring classes = L"Software\\Classes\\";
    const std::wstring command = Quoted(exe) + L" \"%1\"";
    const bool chosen[] = {c.openParts, c.openAssemblies, c.openModules, c.openLibraries, c.openProjects};
    const std::wstring* typeNames[] = {&names.part, &names.assembly, &names.module, &names.library, &names.project};
    for (std::size_t i = 0; i < std::size(kTypes); ++i) {
        const FileType& t = kTypes[i];
        const std::wstring prog = classes + t.progId;
        SetString(HKEY_CURRENT_USER, prog, nullptr, *typeNames[i]);
        SetString(HKEY_CURRENT_USER, prog + L"\\DefaultIcon", nullptr, (c.dir / L"shell" / t.icon).wstring());
        SetString(HKEY_CURRENT_USER, prog + L"\\shell\\open\\command", nullptr, command);
        SetString(HKEY_CURRENT_USER, classes + t.ext + L"\\OpenWithProgids", t.progId, L"");
        SetString(HKEY_CURRENT_USER, classes + L"Applications\\breezecad.exe\\SupportedTypes", t.ext, L"");
        if (chosen[i]) SetString(HKEY_CURRENT_USER, classes + t.ext, nullptr, t.progId);
    }
    SetString(HKEY_CURRENT_USER, classes + L"Applications\\breezecad.exe\\shell\\open\\command", nullptr, command);
    SetString(HKEY_CURRENT_USER, classes + L"Applications\\breezecad.exe", L"FriendlyAppName", L"BreezeCAD");

    // Previews on large icons (parts and assemblies); always, nothing to choose.
    CallThumbs(c.dir / kThumbs, "DllRegisterServer");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool MakeShortcuts(const Choices& c, std::wstring& error) {
    const fs::path exe = c.dir / kExe;
    const fs::path menu = KnownFolder(FOLDERID_Programs) / L"BreezeCAD.lnk";
    const fs::path desk = KnownFolder(FOLDERID_Desktop) / L"BreezeCAD.lnk";
    std::error_code ec;
    bool ok = true;
    if (c.startMenu) ok = Shortcut(menu, exe, L"BreezeCAD", error) && ok;
    else fs::remove(menu, ec);
    if (c.desktop) ok = Shortcut(desk, exe, L"BreezeCAD", error) && ok;
    else fs::remove(desk, ec);
    return ok;
}

bool Uninstall(const fs::path& dir, bool keepSettings, std::wstring& error) {
    std::error_code ec;
    if (ProgramRunning(dir)) {
        error = L"breezecad.exe";
        return false;
    }
    CallThumbs(dir / kThumbs, "DllUnregisterServer");
    // Explorer may still hold the library: moved out of the folder so the folder goes.
    {
        const fs::path dll = dir / kThumbs;
        if (fs::exists(dll, ec) && !fs::remove(dll, ec)) {
            fs::path aside = fs::temp_directory_path(ec) / std::format(L"bz_thumbs-{}.dll", GetTickCount64());
            if (MoveFileExW(dll.c_str(), aside.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
                MoveFileExW(aside.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    }
    for (const std::string& f : ReadList(dir / kFileList))
        if (SafeRelative(f)) fs::remove(dir / fs::path(Wide(f)).make_preferred(), ec);
    for (const wchar_t* f : {kMarker, kFileList, L"uninstall.exe"}) fs::remove(dir / f, ec);
    SweepAside(dir);
    // Empty folders left by the program's files, deepest first; the folder itself last.
    std::vector<fs::path> folders;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_directory(ec)) folders.push_back(it->path());
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) { return a.wstring().size() > b.wstring().size(); });
    for (const fs::path& f : folders) fs::remove(f, ec);   // only empty ones go
    fs::remove(dir, ec);

    const std::wstring classes = L"Software\\Classes\\";
    for (const FileType& t : kTypes) {
        if (GetString(HKEY_CURRENT_USER, classes + t.ext, nullptr) == t.progId) RegDeleteKeyValueW(HKEY_CURRENT_USER, (classes + t.ext).c_str(), nullptr);
        RegDeleteKeyValueW(HKEY_CURRENT_USER, (classes + t.ext + L"\\OpenWithProgids").c_str(), t.progId);
        RegDeleteTreeW(HKEY_CURRENT_USER, (classes + t.progId).c_str());
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, (classes + L"Applications\\breezecad.exe").c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
    fs::remove(KnownFolder(FOLDERID_Programs) / L"BreezeCAD.lnk", ec);
    fs::remove(KnownFolder(FOLDERID_Desktop) / L"BreezeCAD.lnk", ec);
    if (!keepSettings) fs::remove_all(ConfigDir(), ec);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

fs::path SelfPath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n < buffer.size()) {
            buffer.resize(n);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

void Launch(const fs::path& dir) {
    const fs::path exe = dir / kExe;
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

void OpenUrl(const std::wstring& url) { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::optional<fs::path> PickFolder(void* owner, const fs::path& start, const std::wstring& title) {
    Com com;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return std::nullopt;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title.c_str());
    // Start at the nearest folder that exists.
    fs::path at = start;
    std::error_code ec;
    while (!at.empty() && !fs::is_directory(at, ec) && at.parent_path() != at) at = at.parent_path();
    IShellItem* folder = nullptr;
    if (!at.empty() && SUCCEEDED(SHCreateItemFromParsingName(at.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        dialog->SetFolder(folder);
        folder->Release();
    }
    std::optional<fs::path> out;
    if (SUCCEEDED(dialog->Show(static_cast<HWND>(owner)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR raw = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) out = fs::path(raw);
            CoTaskMemFree(raw);
            item->Release();
        }
    }
    dialog->Release();
    return out;
}

} // namespace bz::setup
