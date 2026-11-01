// What the installer does to a Linux computer — the same steps as on Windows
// (Install.cpp), in the desktop's own terms (freedesktop.org):
//
//   the program      ~/.local/share/BreezeCAD/app (or a folder chosen)
//   the record       ~/.local/share/BreezeCAD/install.json: where and which version
//   the menu         ~/.local/share/applications/breezecad.desktop
//   file types       ~/.local/share/mime/packages/breezecad.xml (+ icons, the defaults)
//   previews         ~/.local/share/thumbnailers/breezecad.thumbnailer
//   settings         ~/.config/breezecad (the program's own, as it reads them)
//
// Everything is the person's own; a folder they cannot write to (/opt) takes
// pkexec for the copying only, as Windows takes its administrator prompt.
#include "Install.h"
#include "Strings.h"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

extern char** environ;

namespace bz::setup {
namespace {

namespace fs = std::filesystem;

constexpr const char* kExe = "breezecad";
constexpr const char* kSetup = "breezecad-setup";            // this installer, kept: removal, previews
constexpr const char* kMarker = "installed.json";           // the program reads it: an installed copy
constexpr const char* kFileList = "installed-files.txt";

struct FileType {
    const char* ext;
    const char* mime;
    const char* icon;    ///< installer/icons/png/<icon>.png, embedded
};
constexpr FileType kTypes[] = {
    {"bzc", "application/x-breezecad-part", "part"},
    {"bza", "application/x-breezecad-assembly", "assembly"},
    {"bzmod", "application/x-breezecad-module", "module"},
    {"bzlib", "application/x-breezecad-library", "library"},
    {"bzp", "application/x-breezecad-project", "project"},
};

fs::path Env(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? fs::path(v) : fs::path();
}
fs::path Home() { return Env("HOME"); }
fs::path DataHome() {
    const fs::path x = Env("XDG_DATA_HOME");
    return x.is_absolute() ? x : Home() / ".local" / "share";
}
fs::path Record() { return DataHome() / "BreezeCAD" / "install.json"; }
fs::path DesktopFile() { return DataHome() / "applications" / "breezecad.desktop"; }

/// The desktop folder the person sees (xdg-user-dirs: localized, "Рабочий стол").
fs::path DesktopDir() {
    std::ifstream in(Env("XDG_CONFIG_HOME").is_absolute() ? Env("XDG_CONFIG_HOME") / "user-dirs.dirs" : Home() / ".config" / "user-dirs.dirs");
    for (std::string line; std::getline(in, line);) {
        if (!line.starts_with("XDG_DESKTOP_DIR=")) continue;
        std::string v = line.substr(16);
        if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
        if (v.starts_with("$HOME")) return Home() / v.substr(v.size() > 5 ? 6 : 5);
        return v;
    }
    return Home() / "Desktop";
}

bool WriteAll(const fs::path& file, std::span<const std::uint8_t> bytes, std::wstring& error, int mode = 0644) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // Written beside, then renamed over: a running program keeps its old file
    // (Linux lets it), the new one takes the name.
    const fs::path temp = file.string() + ".bz-new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = Wide(file.string() + ": " + std::strerror(errno));
            return false;
        }
    }
    ::chmod(temp.c_str(), static_cast<mode_t>(mode));
    if (std::rename(temp.c_str(), file.c_str()) != 0) {
        error = Wide(file.string() + ": " + std::strerror(errno));
        fs::remove(temp, ec);
        return false;
    }
    return true;
}
bool WriteText(const fs::path& file, const std::string& text, std::wstring& error, int mode = 0644) {
    return WriteAll(file, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}, error, mode);
}

std::vector<std::string> ReadList(const fs::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);)
        if (!line.empty()) out.push_back(line);
    return out;
}

bool SafeRelative(const std::string& name) {
    if (name.empty() || name.front() == '/') return false;
    std::stringstream parts(name);
    for (std::string part; std::getline(parts, part, '/');)
        if (part == "..") return false;
    return true;
}

/// Runs a program and waits; its exit code (-1: could not start).
int Run(const std::vector<std::string>& argv, bool quiet = true) {
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (quiet) {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    }
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/// Starts a program and leaves it running on its own.
void Detach(const std::vector<std::string>& argv, const fs::path& cwd = {}) {
    const pid_t pid = fork();
    if (pid != 0) {
        if (pid > 0) waitpid(pid, nullptr, 0);
        return;
    }
    // The child forks again and leaves: the grandchild belongs to no one here.
    if (fork() != 0) _exit(0);
    setsid();
    if (!cwd.empty() && chdir(cwd.c_str()) != 0) {}
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
}

/// Whether the person may create files in `dir` (or the nearest folder that exists).
bool CanWrite(fs::path dir) {
    std::error_code ec;
    while (!dir.empty() && !fs::is_directory(dir, ec)) {
        const fs::path up = dir.parent_path();
        if (up == dir) return false;
        dir = up;
    }
    return ::access(dir.c_str(), W_OK) == 0;
}

std::string Quote(const fs::path& p) {   // for Exec= in a desktop file
    std::string out = "\"";
    for (char c : p.string()) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

/// The PNG of a file type or of the program, packed into this installer.
std::span<const std::uint8_t> IconPng(const std::string& name) { return Resource(Wide("ICON_" + name).c_str()); }

void RefreshDesktop() {
    Run({"update-mime-database", (DataHome() / "mime").string()});
    Run({"update-desktop-database", (DataHome() / "applications").string()});
    Run({"gtk-update-icon-cache", "-f", "-t", (DataHome() / "icons" / "hicolor").string()});
}

} // namespace

std::wstring Wide(const std::string& utf8) {
    std::wstring out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = c;
        int extra = 0;
        if (c >= 0xF0) cp = c & 0x07, extra = 3;
        else if (c >= 0xE0) cp = c & 0x0F, extra = 2;
        else if (c >= 0xC0) cp = c & 0x1F, extra = 1;
        ++i;
        for (int k = 0; k < extra && i < utf8.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

std::string Utf8(const std::wstring& wide) {
    std::string out;
    for (wchar_t w : wide) {
        const auto cp = static_cast<char32_t>(w);
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) out += static_cast<char>(0xC0 | (cp >> 6)), out += static_cast<char>(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            out += static_cast<char>(0xE0 | (cp >> 12)), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
                out += static_cast<char>(0x80 | (cp & 0x3F));
        else
            out += static_cast<char>(0xF0 | (cp >> 18)), out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

std::optional<Installed> FindInstalled() {
    std::ifstream in(Record());
    if (!in) return std::nullopt;
    try {
        nlohmann::json j;
        in >> j;
        const fs::path dir = j.value("dir", std::string{});
        std::error_code ec;
        if (dir.empty() || !fs::exists(dir / kExe, ec)) return std::nullopt;
        return Installed{dir, j.value("version", std::string{})};
    } catch (...) {
        return std::nullopt;
    }
}

fs::path DefaultDir() { return DataHome() / "BreezeCAD" / "app"; }

fs::path ConfigDir() {
    // As the program has it (platform::ConfigDir): $XDG_CONFIG_HOME/breezecad,
    // lower case on Linux (it was "BreezeCAD" here: the language, modules and
    // settings went to a folder the program never reads).
    const fs::path x = Env("XDG_CONFIG_HOME");
    return (x.is_absolute() ? x : Home() / ".config") / "breezecad";
}

PathCheck CheckPath(const std::wstring& raw, std::uint64_t needBytes) {
    PathCheck out;
    std::string text = Utf8(raw);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    if (text.empty()) return out;
    if (text.starts_with("~/")) text = Home().string() + text.substr(1);
    if (text.front() != '/') {
        out.state = PathState::NotAbsolute;
        return out;
    }
    const fs::path path = fs::path(text).lexically_normal();
    if (path == "/" || path.parent_path() == path) {
        out.state = PathState::DriveRoot;
        return out;
    }
    if (text.find('\0') != std::string::npos) {
        out.state = PathState::BadCharacters;
        return out;
    }
    for (const char* sys : {"/usr", "/bin", "/sbin", "/lib", "/lib64", "/etc", "/proc", "/sys", "/dev", "/boot", "/run", "/var"}) {
        const std::string s = std::string(sys) + "/";
        if ((path.string() + "/").starts_with(s)) {
            out.state = PathState::SystemFolder;
            return out;
        }
    }
    std::error_code ec;
    if (fs::exists(path, ec)) {
        const bool ours = fs::exists(path / kExe, ec) || fs::exists(path / kMarker, ec);
        const bool empty = fs::is_directory(path, ec) && fs::directory_iterator(path, ec) == fs::directory_iterator();
        if (!fs::is_directory(path, ec) || (!ours && !empty)) {
            out.state = PathState::Foreign;
            return out;
        }
    }
    fs::path probe = path;
    while (!probe.empty() && !fs::exists(probe, ec) && probe.parent_path() != probe) probe = probe.parent_path();
    struct statvfs st{};
    if (::statvfs(probe.c_str(), &st) == 0) out.freeBytes = static_cast<std::uint64_t>(st.f_bavail) * st.f_frsize;
    if (out.freeBytes && out.freeBytes < needBytes) {
        out.state = PathState::NoSpace;
        return out;
    }
    out.state = CanWrite(path) ? PathState::Ok : PathState::NeedsAdmin;
    return out;
}

bool IsElevated() { return ::geteuid() == 0; }

std::optional<unsigned long> RunElevated(const std::wstring& arguments) {
    // pkexec asks for the password in the desktop's own window. The arguments
    // come as the Windows-style command line Install() builds: split on the quotes.
    std::vector<std::string> argv = {"pkexec", SelfPath().string()};
    const std::string line = Utf8(arguments);
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] == ' ') {
            ++i;
            continue;
        }
        if (line[i] == '"') {
            const std::size_t end = line.find('"', i + 1);
            argv.push_back(line.substr(i + 1, end - i - 1));
            i = end == std::string::npos ? line.size() : end + 1;
        } else {
            const std::size_t end = line.find(' ', i);
            argv.push_back(line.substr(i, end - i));
            i = end == std::string::npos ? line.size() : end;
        }
    }
    const int code = Run(argv, false);
    // 126: the person said no; 127: no pkexec here (or not authorised).
    if (code < 0 || code == 126 || code == 127) return std::nullopt;
    return static_cast<unsigned long>(code);
}

bool ProgramRunning(const fs::path& dir) {
    std::error_code ec;
    const fs::path exe = fs::weakly_canonical(dir / kExe, ec);
    for (const auto& e : fs::directory_iterator("/proc", ec)) {
        const std::string pid = e.path().filename().string();
        if (pid.empty() || !std::isdigit(static_cast<unsigned char>(pid[0]))) continue;
        std::error_code ec2;
        const fs::path target = fs::read_symlink(e.path() / "exe", ec2);
        if (!ec2 && target == exe) return true;
    }
    return false;
}

bool Unzip(std::span<const std::uint8_t> bytes, const fs::path& dir, bool stripTop, const Progress& progress,
           std::vector<std::string>* written, std::wstring& error) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        error = L"ZIP";
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    std::string top;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        total += st.m_uncomp_size;
        const std::string name = st.m_filename;
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
        if (!top.empty() && name.starts_with(top)) name.erase(0, top.size());
        if (name.empty()) continue;
        if (!SafeRelative(name)) {
            error = Wide("ZIP: " + name);
            ok = false;
            break;
        }
        const fs::path target = dir / name;
        if (st.m_is_directory) {
            fs::create_directories(target, ec);
            continue;
        }
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data && st.m_uncomp_size) {
            error = Wide("ZIP: " + name);
            ok = false;
            break;
        }
        // The Unix permissions the archive keeps (executables stay executable).
        const unsigned mode = (st.m_external_attr >> 16) & 0777;
        ok = WriteAll(target, {static_cast<const std::uint8_t*>(data), size}, error, mode ? static_cast<int>(mode) : 0644);
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
        if (!keep.contains(old) && SafeRelative(old)) fs::remove(dir / old, ec);
    std::ofstream out(dir / kFileList, std::ios::trunc);
    for (const std::string& f : now) out << f << '\n';
}

bool PlaceModule(std::span<const std::uint8_t> package, const std::string& fileName, std::wstring& error) {
    return WriteAll(ConfigDir() / "modules" / fileName, package, error);
}

bool PlaceLanguage(std::span<const std::uint8_t> pack, std::wstring& error) { return Unzip(pack, ConfigDir() / "lang", true, {}, nullptr, error); }

void SetProgramLanguage(const std::string& code) {
    const fs::path file = ConfigDir() / "settings.json";
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    nlohmann::json j = nlohmann::json::object();
    if (std::ifstream in(file); in) {
        try {
            in >> j;
        } catch (...) {
            return;
        }
        if (!j.is_object()) return;
    }
    j["language"] = code;
    std::ofstream(file, std::ios::trunc) << j.dump(1, '\t');
}

bool FinishFolder(const fs::path& dir, const std::string& version, std::span<const std::uint8_t> installerExe, std::wstring& error) {
    if (!WriteAll(dir / kSetup, installerExe, error, 0755)) return false;
    const nlohmann::json marker = {{"version", version}, {"by", "BreezeCAD Setup"}};
    return WriteText(dir / kMarker, marker.dump(1, '\t'), error);
}

bool Register(const Choices& c, const std::string& version, std::uint64_t sizeBytes, const TypeNames& names, std::wstring& error) {
    (void)sizeBytes;
    const fs::path data = DataHome();
    // Where it is: for the next run of the installer (update, removal).
    const nlohmann::json record = {{"dir", c.dir.string()}, {"version", version}};
    if (!WriteText(Record(), record.dump(1, '\t'), error)) return false;

    // File types: what they are, how they look, which open in BreezeCAD.
    const std::wstring* typeNames[] = {&names.part, &names.assembly, &names.module, &names.library, &names.project};
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";
    for (std::size_t i = 0; i < std::size(kTypes); ++i) {
        const FileType& t = kTypes[i];
        std::string comment = Utf8(*typeNames[i]);
        for (const auto& [from, to] : {std::pair{"&", "&amp;"}, {"<", "&lt;"}, {">", "&gt;"}})
            for (std::size_t at = 0; (at = comment.find(from, at)) != std::string::npos; at += std::strlen(to)) comment.replace(at, 1, to);
        xml += std::format("  <mime-type type=\"{}\">\n    <comment>{}</comment>\n    <sub-class-of type=\"application/zip\"/>\n"
                           "    <glob pattern=\"*.{}\" weight=\"80\"/>\n    <icon name=\"{}\"/>\n  </mime-type>\n",
                           t.mime, comment, t.ext, std::string(t.mime).replace(std::string(t.mime).find('/'), 1, "-"));
        const auto png = IconPng(t.icon);
        if (!png.empty()) {
            std::string iconName = t.mime;
            iconName.replace(iconName.find('/'), 1, "-");
            WriteAll(data / "icons" / "hicolor" / "256x256" / "mimetypes" / (iconName + ".png"), png, error);
        }
    }
    xml += "</mime-info>\n";
    if (!WriteText(data / "mime" / "packages" / "breezecad.xml", xml, error)) return false;
    if (const auto png = IconPng("app"); !png.empty()) WriteAll(data / "icons" / "hicolor" / "256x256" / "apps" / "breezecad.png", png, error);

    // Previews in the file manager (Nautilus, Nemo, Caja, Thunar's tumbler):
    // the picture each file keeps inside, taken out by this installer kept in
    // the program's folder (`--thumbnail`).
    std::string mimes;
    for (const FileType& t : kTypes) mimes += std::string(t.mime) + ";";
    WriteText(data / "thumbnailers" / "breezecad.thumbnailer",
              std::format("[Thumbnailer Entry]\nTryExec={}\nExec={} --thumbnail %i %o %s\nMimeType={}\n", (c.dir / kSetup).string(),
                          Quote(c.dir / kSetup), mimes),
              error);

    // The program in the menu (hidden when not wanted there: "Open with" still knows it).
    const std::string desktop = std::format(
        "[Desktop Entry]\nType=Application\nName=BreezeCAD\nGenericName=CAD\nComment={}\nExec={} %F\nIcon=breezecad\n"
        "Terminal=false\nCategories=Graphics;Engineering;3DGraphics;\nMimeType={}\nStartupWMClass=BreezeCAD\nNoDisplay={}\n",
        Tr("tagline"), Quote(c.dir / kExe), mimes, c.startMenu ? "false" : "true");
    if (!WriteText(DesktopFile(), desktop, error)) return false;
    RefreshDesktop();
    // The chosen types open in BreezeCAD by default.
    const bool chosen[] = {c.openParts, c.openAssemblies, c.openModules, c.openLibraries, c.openProjects};
    for (std::size_t i = 0; i < std::size(kTypes); ++i)
        if (chosen[i]) Run({"xdg-mime", "default", "breezecad.desktop", kTypes[i].mime});
    return true;
}

bool MakeShortcuts(const Choices& c, std::wstring& error) {
    // The menu entry is the desktop file Register wrote; the desktop gets a copy.
    const fs::path link = DesktopDir() / "breezecad.desktop";
    std::error_code ec;
    if (!c.desktop) {
        fs::remove(link, ec);
        return true;
    }
    std::ifstream in(DesktopFile());
    std::string text{std::istreambuf_iterator<char>(in), {}};
    if (const auto at = text.find("NoDisplay=true"); at != std::string::npos) text.replace(at, 14, "NoDisplay=false");
    if (!WriteText(link, text, error, 0755)) return false;
    // GNOME runs a desktop launcher only once it is marked trusted.
    Run({"gio", "set", link.string(), "metadata::trusted", "true"});
    return true;
}

bool Uninstall(const fs::path& dir, bool keepSettings, std::wstring& error) {
    std::error_code ec;
    if (ProgramRunning(dir)) {
        error = L"breezecad";
        return false;
    }
    for (const std::string& f : ReadList(dir / kFileList))
        if (SafeRelative(f)) fs::remove(dir / f, ec);
    for (const char* f : {kMarker, kFileList, kSetup}) fs::remove(dir / f, ec);
    std::vector<fs::path> folders;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_directory(ec)) folders.push_back(it->path());
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) { return a.string().size() > b.string().size(); });
    for (const fs::path& f : folders) fs::remove(f, ec);
    fs::remove(dir, ec);

    const fs::path data = DataHome();
    fs::remove(DesktopFile(), ec);
    fs::remove(DesktopDir() / "breezecad.desktop", ec);
    fs::remove(data / "mime" / "packages" / "breezecad.xml", ec);
    fs::remove(data / "thumbnailers" / "breezecad.thumbnailer", ec);
    fs::remove(data / "icons" / "hicolor" / "256x256" / "apps" / "breezecad.png", ec);
    for (const FileType& t : kTypes) {
        std::string iconName = t.mime;
        iconName.replace(iconName.find('/'), 1, "-");
        fs::remove(data / "icons" / "hicolor" / "256x256" / "mimetypes" / (iconName + ".png"), ec);
    }
    fs::remove(Record(), ec);
    fs::remove(Record().parent_path(), ec);   // only when empty
    // The defaults set for its types go too (else they point at a file no longer there).
    const fs::path apps = (Env("XDG_CONFIG_HOME").is_absolute() ? Env("XDG_CONFIG_HOME") : Home() / ".config") / "mimeapps.list";
    if (std::ifstream in(apps); in) {
        std::string kept;
        for (std::string line; std::getline(in, line);)
            if (line.find("breezecad.desktop") == std::string::npos) kept += line + "\n";
        in.close();
        std::wstring ignored;
        WriteText(apps, kept, ignored);
    }
    if (!keepSettings) fs::remove_all(ConfigDir(), ec);
    RefreshDesktop();
    return true;
}

fs::path SelfPath() {
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
}

void Launch(const fs::path& dir) { Detach({(dir / kExe).string()}, dir); }

void OpenUrl(const std::wstring& url) { Detach({"xdg-open", Utf8(url)}); }

std::optional<fs::path> PickFolder(void* owner, const fs::path& start, const std::wstring& title) {
    (void)owner;
    // The desktop's own folder dialog: zenity (GNOME and most), kdialog (KDE).
    fs::path at = start;
    std::error_code ec;
    while (!at.empty() && !fs::is_directory(at, ec) && at.parent_path() != at) at = at.parent_path();
    const std::string t = Utf8(title);
    for (const std::vector<std::string>& cmd :
         {std::vector<std::string>{"zenity", "--file-selection", "--directory", "--title=" + t, "--filename=" + at.string() + "/"},
          std::vector<std::string>{"kdialog", "--getexistingdirectory", at.string(), "--title", t}}) {
        // popen with the words quoted: titles and paths may have spaces.
        std::string line;
        for (const std::string& w : cmd) {
            line += "'";
            for (char ch : w) line += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
            line += "' ";
        }
        line += "2>/dev/null";
        FILE* p = ::popen(line.c_str(), "r");
        if (!p) continue;
        std::string out;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), p)) out += buffer;
        const int status = ::pclose(p);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) continue;   // that dialog is not installed
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (out.empty()) return std::nullopt;   // cancelled
        return fs::path(out);
    }
    return std::nullopt;
}

} // namespace bz::setup
