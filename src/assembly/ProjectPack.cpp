#include "assembly/ProjectPack.h"

#include "assembly/AssemblyFile.h"
#include "core/PathText.h"
#include "document/BzcFile.h"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace bz::assembly {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr const char* kInstruction = "project.json";

Expected<std::string> ReadBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);   // a wide path on Windows: names are often Cyrillic
    if (!in) return Error{std::format("Не удалось прочитать {}", ToUtf8(file.filename()))};
    return std::string(std::istreambuf_iterator<char>(in), {});
}

/// A path inside the archive that stays inside the folder it is unpacked to.
bool SafeInside(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != std::string::npos) return false;
    std::stringstream parts(path);
    for (std::string part; std::getline(parts, part, '/');)
        if (part.empty() || part == "." || part == ".." || part.find('\\') != std::string::npos) return false;
    return true;
}

/// "Вал.bzc" → "Вал (2).bzc" while taken.
std::string FreeName(const std::string& wanted, const std::set<std::string>& taken) {
    if (!taken.contains(wanted)) return wanted;
    const fs::path p = PathFromUtf8(wanted);
    const std::string stem = ToUtf8(p.stem()), ext = ToUtf8(p.extension());
    for (int n = 2;; ++n) {
        std::string name = std::format("{} ({}){}", stem, n, ext);
        if (!taken.contains(name)) return name;
    }
}

struct Zip {
    mz_zip_archive z{};
    bool           open = false;
    ~Zip() {
        if (open) mz_zip_reader_end(&z);
    }
};

} // namespace

Expected<PackReport> PackProject(const fs::path& assemblyFile, const fs::path& out, std::string_view version) {
    Expected<Assembly> loaded = LoadAssembly(assemblyFile);
    if (!loaded) return loaded.GetError();
    Assembly a = std::move(*loaded);
    const std::string mainName = ToUtf8(assemblyFile.filename());

    // Where each part goes: as the assembly refers to it when that is inside
    // its folder, else next to it (renamed when the name is taken).
    PackReport report;
    std::set<std::string> taken{mainName, kInstruction};
    std::map<std::string, std::string> inside;   // part file (absolute, generic) → path in the archive
    std::map<std::string, ProjectFile> files;
    std::vector<std::pair<std::string, fs::path>> toPack;
    for (Component& c : a.components) {
        const fs::path file = ResolvePartPath(assemblyFile, c.path);
        std::error_code ec;
        const std::string key = fs::weakly_canonical(file, ec).generic_string();
        auto it = inside.find(key);
        if (it == inside.end()) {
            if (!fs::exists(file, ec)) {
                report.missing.push_back(c.name);
                continue;
            }
            std::string where = c.path;
            for (char& ch : where)
                if (ch == '\\') ch = '/';
            if (!SafeInside(where) || taken.contains(where)) where = FreeName(ToUtf8(file.filename()), taken);
            taken.insert(where);
            it = inside.emplace(key, where).first;
            toPack.emplace_back(where, file);
            files[where] = ProjectFile{where, "part", {}};
            ++report.parts;
        }
        c.path = it->second;
        files[it->second].components.push_back(c.name);
    }

    // The assembly with its paths as they are in the archive (and its picture).
    const fs::path temp = out.parent_path() / PathFromUtf8("." + ToUtf8(out.filename()) + ".assembly");
    std::vector<std::uint8_t> preview;
    if (auto p = doc::ReadBzcPreview(assemblyFile); p) preview = std::move(*p);
    if (Status s = SaveAssembly(a, temp, "BreezeCAD project", preview); !s) return s.GetError();
    Expected<std::string> assemblyBytes = ReadBytes(temp);
    std::error_code ec;
    fs::remove(temp, ec);
    if (!assemblyBytes) return assemblyBytes.GetError();

    Json list = Json::array();
    list.push_back({{"path", mainName}, {"role", "assembly"}});
    for (const auto& [path, f] : files) list.push_back({{"path", f.path}, {"role", f.role}, {"components", f.components}});
    const Json instruction{{"format", "bzp"},
                           {"version", kProjectFormatVersion},
                           {"createdBy", std::string(version)},
                           {"name", ToUtf8(assemblyFile.stem())},
                           {"main", mainName},
                           {"files", list}};
    const std::string text = instruction.dump(1, '\t');

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 20)) return Error{"Не удалось подготовить архив"};
    bool ok = mz_zip_writer_add_mem(&zip, kInstruction, text.data(), text.size(), MZ_BEST_COMPRESSION) &&
              mz_zip_writer_add_mem(&zip, mainName.c_str(), assemblyBytes->data(), assemblyBytes->size(), MZ_NO_COMPRESSION);
    // The assembly's picture at the top too: file managers show it for the project.
    if (!preview.empty()) ok = ok && mz_zip_writer_add_mem(&zip, "preview.png", preview.data(), preview.size(), MZ_NO_COMPRESSION);
    for (const auto& [where, file] : toPack) {
        Expected<std::string> bytes = ReadBytes(file);
        if (!bytes) {
            mz_zip_writer_end(&zip);
            return bytes.GetError();
        }
        // Documents are ZIPs already: stored as they are.
        ok = ok && mz_zip_writer_add_mem(&zip, where.c_str(), bytes->data(), bytes->size(), MZ_NO_COMPRESSION);
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    std::string archive = ok ? std::string(static_cast<const char*>(buffer), size) : std::string();
    mz_zip_writer_end(&zip);
    if (!ok) return Error{"Не удалось упаковать проект"};
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    file.write(archive.data(), static_cast<std::streamsize>(archive.size()));
    if (!file) return Error{std::format("Не удалось записать {}", ToUtf8(out.filename()))};
    return report;
}

Expected<ProjectManifest> ReadProject(const fs::path& bzp) {
    Expected<std::string> bytes = ReadBytes(bzp);
    if (!bytes) return bytes.GetError();
    Zip zip;
    if (!mz_zip_reader_init_mem(&zip.z, bytes->data(), bytes->size(), 0)) return Error{"Это не проект BreezeCAD: файл не ZIP-архив"};
    zip.open = true;
    const int index = mz_zip_reader_locate_file(&zip.z, kInstruction, nullptr, 0);
    if (index < 0) return Error{"Это не проект BreezeCAD: в архиве нет project.json"};
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&zip.z, static_cast<mz_uint>(index), &size, 0);
    if (!data) return Error{"Проект повреждён: не читается project.json"};
    const std::string text(static_cast<const char*>(data), size);
    mz_free(data);
    try {
        const Json j = Json::parse(text);
        if (j.value("format", std::string{}) != "bzp") return Error{"Это не проект BreezeCAD"};
        ProjectManifest m;
        m.name = j.value("name", std::string{});
        m.main = j.value("main", std::string{});
        m.createdBy = j.value("createdBy", std::string{});
        for (const Json& f : j.value("files", Json::array())) {
            ProjectFile pf;
            pf.path = f.value("path", std::string{});
            pf.role = f.value("role", std::string{});
            for (const Json& c : f.value("components", Json::array()))
                if (c.is_string()) pf.components.push_back(c.get<std::string>());
            if (!SafeInside(pf.path)) return Error{std::format("Проект небезопасен: путь «{}» ведёт за пределы папки", pf.path)};
            m.files.push_back(std::move(pf));
        }
        if (!SafeInside(m.main)) return Error{"Проект повреждён: не указана главная сборка"};
        if (j.value("version", 0) > kProjectFormatVersion && m.files.empty()) return Error{"Проект создан более новой версией BreezeCAD"};
        return m;
    } catch (const Json::exception&) {
        return Error{"Проект повреждён: project.json не читается"};
    }
}

Expected<fs::path> UnpackProject(const fs::path& bzp, const fs::path& folder) {
    Expected<ProjectManifest> m = ReadProject(bzp);
    if (!m) return m.GetError();
    Expected<std::string> bytes = ReadBytes(bzp);
    if (!bytes) return bytes.GetError();
    Zip zip;
    if (!mz_zip_reader_init_mem(&zip.z, bytes->data(), bytes->size(), 0)) return Error{"Проект повреждён"};
    zip.open = true;
    std::error_code ec;
    // Nothing is written until it is clear nothing will be overwritten.
    for (const ProjectFile& f : m->files)
        if (fs::exists(folder / PathFromUtf8(f.path), ec))
            return Error{std::format("В папке уже есть «{}»: выберите другую папку", f.path)};
    fs::create_directories(folder, ec);
    for (const ProjectFile& f : m->files) {
        const int index = mz_zip_reader_locate_file(&zip.z, f.path.c_str(), nullptr, 0);
        if (index < 0) return Error{std::format("Проект повреждён: в архиве нет «{}»", f.path)};
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip.z, static_cast<mz_uint>(index), &size, 0);
        if (!data && size) return Error{std::format("Проект повреждён: не читается «{}»", f.path)};
        const fs::path target = folder / PathFromUtf8(f.path);
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        mz_free(data);
        if (!out) return Error{std::format("Не удалось записать «{}» (диск заполнен или нет доступа к папке?)", f.path)};
    }
    return folder / PathFromUtf8(m->main);
}

} // namespace bz::assembly
