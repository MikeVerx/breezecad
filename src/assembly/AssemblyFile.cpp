#include "assembly/AssemblyFile.h"

#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"
#include "document/JsonIO.h"

#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <utility>

namespace bz::assembly {
namespace {

using doc::json_io::Json;
using doc::json_io::ReadMember;
namespace jio = doc::json_io;

constexpr std::array<std::pair<GeometryKind, std::string_view>, 6> kKinds{{
    {GeometryKind::Plane, "plane"},
    {GeometryKind::Axis, "axis"},
    {GeometryKind::Point, "point"},
    {GeometryKind::Cylinder, "cylinder"},
    {GeometryKind::Sphere, "sphere"},
    {GeometryKind::Circle, "circle"},
}};

constexpr std::array<std::pair<ConstraintType, std::string_view>, 11> kTypes{{
    {ConstraintType::Mate, "mate"},
    {ConstraintType::Insert, "insert"},
    {ConstraintType::Angle, "angle"},
    {ConstraintType::Tangent, "tangent"},
    {ConstraintType::AxisMate, "axisMate"},
    {ConstraintType::Rotational, "jointRotational"},
    {ConstraintType::Slider, "jointSlider"},
    {ConstraintType::Cylindrical, "jointCylindrical"},
    {ConstraintType::Planar, "jointPlanar"},
    {ConstraintType::Ball, "jointBall"},
    {ConstraintType::Rigid, "jointRigid"},
}};

Json ToJson(const Placement& p) {
    // The part's axes in the assembly, like a work plane's frame: readable
    // and exact (no Euler angles to get wrong).
    return Json{{"origin", jio::ToJson(p.translation)},
                {"xAxis", jio::ToJson(p.rotation.col[0])},
                {"zAxis", jio::ToJson(p.rotation.col[2])}};
}

bool Read(const Json& j, Placement& out) {
    if (!j.is_object()) return false;
    Vec3 origin, x{1, 0, 0}, z{0, 0, 1};
    if (!ReadMember(j, "origin", origin) || !ReadMember(j, "xAxis", x) || !ReadMember(j, "zAxis", z)) return false;
    if (x.Length() < 1e-9 || z.Length() < 1e-9) return false;
    out.translation = origin;
    out.rotation    = Rotation::FromXZ(x, z);
    return true;
}

Json ToJson(const GeometryRef& g) {
    Json j{{"component", g.component.value},
           {"kind", jio::IdOf(kKinds, g.kind)},
           {"origin", jio::ToJson(g.origin)},
           {"pick", jio::ToJson(g.pick)}};
    if (g.direction.Length() > 0) j["direction"] = jio::ToJson(g.direction);
    if (g.radius > 0) j["radius"] = g.radius;
    if (g.reference.Length() > 0) j["reference"] = jio::ToJson(g.reference);
    return j;
}

constexpr std::array<std::pair<PatternKind, std::string_view>, 3> kPatternKinds{{
    {PatternKind::Holes, "holes"},
    {PatternKind::Rectangular, "rectangular"},
    {PatternKind::Circular, "circular"},
}};

Json IdsToJson(const std::vector<ObjectId>& ids) {
    Json j = Json::array();
    for (ObjectId id : ids) j.push_back(id.value);
    return j;
}

bool ReadIds(const Json& j, std::vector<ObjectId>& out) {
    if (!j.is_array()) return false;
    for (const Json& v : j) {
        if (!v.is_number_unsigned()) return false;
        out.push_back(ObjectId{v.get<std::uint64_t>()});
    }
    return true;
}

bool Read(const Json& j, GeometryRef& out) {
    if (!j.is_object()) return false;
    GeometryRef g;
    const std::string kind = j.value("kind", "");
    if (!jio::FromId(kKinds, kind, g.kind)) return false;
    if (!ReadMember(j, "component", g.component) || !ReadMember(j, "origin", g.origin) ||
        !ReadMember(j, "direction", g.direction) || !ReadMember(j, "radius", g.radius) ||
        !ReadMember(j, "pick", g.pick) || !ReadMember(j, "reference", g.reference))
        return false;
    out = g;
    return true;
}

} // namespace

std::string AssemblyToJson(const Assembly& a) {
    Json components = Json::array();
    for (const Component& c : a.components) {
        Json j{{"id", c.id.value},
               {"name", c.name},
               {"path", c.path},
               {"placement", ToJson(c.placement)},
               {"properties",
                {{"designation", c.properties.designation},
                 {"name", c.properties.name},
                 {"material", c.properties.material},
                 {"quantity", c.properties.quantity}}}};
        if (c.grounded) j["grounded"] = true;
        if (!c.visible) j["visible"] = false;
        components.push_back(std::move(j));
    }
    Json constraints = Json::array();
    for (const Constraint& c : a.constraints) {
        Json j{{"id", c.id.value},
               {"name", c.name},
               {"type", jio::IdOf(kTypes, c.type)},
               {"first", ToJson(c.first)},
               {"second", ToJson(c.second)},
               {"value", c.value}};
        if (c.flipped) j["flipped"] = true;
        if (c.suppressed) j["suppressed"] = true;
        if (c.locked) j["locked"] = true;
        if (c.locked || c.position != 0.0) j["position"] = c.position;
        if (c.limits.enabled) j["limits"] = Json{{"lower", c.limits.lower}, {"upper", c.limits.upper}};
        constraints.push_back(std::move(j));
    }
    Json root{
        {"assembly", {{"name", a.info.name}, {"designation", a.info.designation}}},
        {"formatVersion", kAssemblyFormatVersion},
        {"nextId", a.Ids().Last() + 1},
        {"components", std::move(components)},
        {"constraints", std::move(constraints)},
    };
    if (!a.look.Empty()) root["look"] = doc::ToJson(a.look);   // pictures go into the archive
    if (!a.patterns.empty()) {
        Json patterns = Json::array();
        for (const ComponentPattern& p : a.patterns) {
            Json j{{"id", p.id.value}, {"name", p.name}, {"kind", jio::IdOf(kPatternKinds, p.kind)}, {"seed", IdsToJson(p.seed)}};
            switch (p.kind) {
                case PatternKind::Holes: j["rim"] = ToJson(p.rim); break;
                case PatternKind::Rectangular:
                    j["direction1"] = jio::ToJson(p.direction1);
                    j["count1"] = p.count1;
                    j["spacing1"] = p.spacing1;
                    j["direction2"] = jio::ToJson(p.direction2);
                    j["count2"] = p.count2;
                    j["spacing2"] = p.spacing2;
                    break;
                case PatternKind::Circular:
                    if (p.axis.IsSet()) j["axis"] = ToJson(p.axis);
                    j["axisOrigin"] = jio::ToJson(p.axisOrigin);
                    j["axisDirection"] = jio::ToJson(p.axisDirection);
                    j["count"] = p.count;
                    j["angle"] = p.angle;
                    break;
            }
            Json instances = Json::array();
            for (const auto& instance : p.instances) instances.push_back(IdsToJson(instance));
            j["instances"] = std::move(instances);
            patterns.push_back(std::move(j));
        }
        root["patterns"] = std::move(patterns);
    }
    return root.dump(1, '\t');
}

namespace {

Expected<Assembly> AssemblyFromJsonUnguarded(std::string_view text, std::vector<std::string>* warnings) {
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return Error{"Содержимое сборки повреждено (не JSON)"};

    Assembly a;
    if (const auto info = root.find("assembly"); info != root.end() && info->is_object()) {
        a.info.name        = info->value("name", "");
        a.info.designation = info->value("designation", "");
    }
    auto reserve = [&](ObjectId id) { a.Ids().Reserve(id); };

    for (const Json& j : root.value("components", Json::array())) {
        Component c;
        if (!j.is_object() || !ReadMember(j, "id", c.id) || !c.id || !ReadMember(j, "name", c.name) ||
            !ReadMember(j, "path", c.path) || !ReadMember(j, "grounded", c.grounded) ||
            !ReadMember(j, "visible", c.visible))
            return Error{"Повреждён компонент сборки"};
        if (const auto p = j.find("placement"); p != j.end() && !Read(*p, c.placement))
            return Error{std::format("Повреждено положение компонента «{}»", c.name)};
        if (const auto p = j.find("properties"); p != j.end() && p->is_object()) {
            if (!ReadMember(*p, "designation", c.properties.designation) || !ReadMember(*p, "name", c.properties.name) ||
                !ReadMember(*p, "material", c.properties.material) || !ReadMember(*p, "quantity", c.properties.quantity))
                return Error{std::format("Повреждены свойства компонента «{}»", c.name)};
        }
        if (c.path.empty()) return Error{std::format("У компонента «{}» нет файла детали", c.name)};
        reserve(c.id);
        a.components.push_back(std::move(c));
    }

    for (const Json& j : root.value("constraints", Json::array())) {
        Constraint c;
        if (!j.is_object()) return Error{"Повреждена зависимость сборки"};
        const std::string type = j.value("type", "");
        if (!jio::FromId(kTypes, type, c.type)) {
            if (warnings) warnings->push_back(std::format("Зависимость типа «{}» не поддерживается и пропущена", type));
            continue;
        }
        const auto first = j.find("first"), second = j.find("second");
        if (!ReadMember(j, "id", c.id) || !c.id || !ReadMember(j, "name", c.name) ||
            !ReadMember(j, "value", c.value) || !ReadMember(j, "flipped", c.flipped) ||
            !ReadMember(j, "suppressed", c.suppressed) || first == j.end() || second == j.end() ||
            !Read(*first, c.first) || !Read(*second, c.second) || !ReadMember(j, "locked", c.locked) ||
            !ReadMember(j, "position", c.position))
            return Error{"Повреждена зависимость сборки"};
        if (const auto lim = j.find("limits"); lim != j.end()) {
            if (!lim->is_object() || !ReadMember(*lim, "lower", c.limits.lower) || !ReadMember(*lim, "upper", c.limits.upper))
                return Error{"Повреждена зависимость сборки"};
            c.limits.enabled = true;
        }
        if (!a.FindComponent(c.first.component) || !a.FindComponent(c.second.component)) {
            if (warnings) warnings->push_back(std::format("Зависимость «{}» ссылается на удалённый компонент и пропущена", c.name));
            continue;
        }
        reserve(c.id);
        a.constraints.push_back(std::move(c));
    }

    for (const Json& j : root.value("patterns", Json::array())) {
        ComponentPattern p;
        if (!j.is_object()) return Error{"Повреждён массив компонентов"};
        const std::string kind = j.value("kind", "");
        if (!jio::FromId(kPatternKinds, kind, p.kind)) {
            if (warnings) warnings->push_back(std::format("Массив вида «{}» не поддерживается и пропущен", kind));
            continue;
        }
        const auto seed = j.find("seed");
        if (!ReadMember(j, "id", p.id) || !p.id || !ReadMember(j, "name", p.name) || seed == j.end() ||
            !ReadIds(*seed, p.seed) || !ReadMember(j, "direction1", p.direction1) || !ReadMember(j, "count1", p.count1) ||
            !ReadMember(j, "spacing1", p.spacing1) || !ReadMember(j, "direction2", p.direction2) ||
            !ReadMember(j, "count2", p.count2) || !ReadMember(j, "spacing2", p.spacing2) ||
            !ReadMember(j, "axisOrigin", p.axisOrigin) || !ReadMember(j, "axisDirection", p.axisDirection) ||
            !ReadMember(j, "count", p.count) || !ReadMember(j, "angle", p.angle))
            return Error{"Повреждён массив компонентов"};
        if (const auto r = j.find("rim"); r != j.end() && !Read(*r, p.rim)) return Error{std::format("Повреждён массив «{}»", p.name)};
        if (const auto r = j.find("axis"); r != j.end() && !Read(*r, p.axis)) return Error{std::format("Повреждён массив «{}»", p.name)};
        for (const Json& instance : j.value("instances", Json::array())) {
            std::vector<ObjectId> ids;
            if (!ReadIds(instance, ids)) return Error{std::format("Повреждён массив «{}»", p.name)};
            std::erase_if(ids, [&](ObjectId id) { return !a.FindComponent(id); });
            if (!ids.empty()) p.instances.push_back(std::move(ids));
        }
        std::erase_if(p.seed, [&](ObjectId id) { return !a.FindComponent(id); });
        if (p.seed.empty()) {
            if (warnings) warnings->push_back(std::format("Массив «{}» ссылается на удалённые компоненты и пропущен", p.name));
            continue;
        }
        reserve(p.id);
        a.patterns.push_back(std::move(p));
    }

    if (const auto look = root.find("look"); look != root.end()) a.look = doc::LookFromJson(*look);   // absent in older files

    if (std::uint64_t next = root.value("nextId", std::uint64_t{0}); next > 0) a.Ids().Reserve(ObjectId{next - 1});
    if (root.value("formatVersion", 0) > kAssemblyFormatVersion && warnings)
        warnings->push_back("Сборка создана более новой версией BreezeCAD. Новые возможности могут не открыться.");
    return a;
}

} // namespace

// See doc::DocumentFromJson: a field of the wrong type throws inside nlohmann::json.
Expected<Assembly> AssemblyFromJson(std::string_view text, std::vector<std::string>* warnings) {
    try {
        return AssemblyFromJsonUnguarded(text, warnings);
    } catch (const Json::exception& e) {
        return Error{std::format("Содержимое сборки повреждено: {}", e.what())};
    }
}

Status SaveAssembly(const Assembly& assembly, const std::filesystem::path& path, std::string_view generator) {
    // Only the pictures its looks still use.
    std::map<std::string, std::vector<std::uint8_t>> pictures;
    for (const doc::Appearance& a : assembly.look.used)
        if (const auto it = assembly.look.pictures.find(a.picture); a.HasPicture() && it != assembly.look.pictures.end())
            pictures.insert(*it);
    return doc::WriteBzcArchive(path, doc::kAssemblyType, generator, AssemblyToJson(assembly), pictures);
}

Expected<Assembly> LoadAssembly(const std::filesystem::path& path, std::vector<std::string>* warnings) {
    Expected<doc::BzcArchive> archive = doc::ReadBzcArchive(path);
    if (!archive) return archive.GetError();
    if (archive->documentType != doc::kAssemblyType) {
        const doc::DocumentTypeInfo* type = doc::DocumentTypeRegistry::Default().Find(archive->documentType);
        return Error{std::format("Это не сборка, а документ «{}»", type ? type->title : archive->documentType)};
    }
    Expected<Assembly> assembly = AssemblyFromJson(archive->documentJson, warnings);
    if (assembly) assembly->look.pictures = std::move(archive->pictures);
    return assembly;
}

/// '/' separators on every system, so a file saved on Windows opens elsewhere.
static std::string Generic(const std::filesystem::path& p) {
    const std::u8string text = p.generic_u8string();
    return {text.begin(), text.end()};
}

std::string RelativePartPath(const std::filesystem::path& assemblyFile, const std::filesystem::path& part) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(part, ec);
    if (assemblyFile.empty()) return Generic(absolute.lexically_normal());
    const std::filesystem::path base = std::filesystem::absolute(assemblyFile, ec).parent_path();
    const std::filesystem::path relative = absolute.lexically_normal().lexically_relative(base.lexically_normal());
    if (relative.empty()) return Generic(absolute.lexically_normal());   // another drive
    return Generic(relative);
}

std::filesystem::path ResolvePartPath(const std::filesystem::path& assemblyFile, std::string_view componentPath) {
    const std::filesystem::path p = PathFromUtf8(componentPath);
    if (p.is_absolute() || assemblyFile.empty()) return p.lexically_normal();
    std::error_code ec;
    return (std::filesystem::absolute(assemblyFile, ec).parent_path() / p).lexically_normal();
}

void RebasePaths(Assembly& assembly, const std::filesystem::path& oldFile, const std::filesystem::path& newFile) {
    for (Component& c : assembly.components) c.path = RelativePartPath(newFile, ResolvePartPath(oldFile, c.path));
}

namespace {

namespace fs = std::filesystem;

/// The same place however written ("a/../b", a link, the letter case on Windows).
fs::path Settled(const fs::path& p) {
    std::error_code ec;
    fs::path out = fs::weakly_canonical(p, ec);
    if (ec) out = fs::absolute(p, ec).lexically_normal();
    return out;
}

std::optional<std::string> Bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool EndsWith(std::string_view text, std::string_view tail) {
    return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail;
}

} // namespace

std::string CopyName(const fs::path& part, const fs::path& assemblyFile, const fs::path& previousAssemblyFile) {
    std::string stem = ToUtf8(part.stem());
    const std::string assembly = ToUtf8(assemblyFile.stem());
    // A copy made for this assembly or the one it was saved as before: its part's own name.
    for (const fs::path& a : {assemblyFile, previousAssemblyFile}) {
        if (a.empty()) continue;
        const std::string tail = " - " + ToUtf8(a.stem());
        if (EndsWith(stem, tail) && stem.size() > tail.size()) {
            stem.resize(stem.size() - tail.size());
            break;
        }
    }
    std::string ext = ToUtf8(part.extension());
    if (ext.empty()) ext = ".bzc";
    return stem + " - " + assembly + ext;
}

bool LiesWith(const fs::path& assemblyFile, const fs::path& part, bool subfolders) {
    if (assemblyFile.empty()) return false;
    const fs::path folder = Settled(assemblyFile).parent_path();
    const fs::path file = Settled(part);
    if (!subfolders) return file.parent_path() == folder;
    const fs::path relative = file.lexically_relative(folder);
    return !relative.empty() && *relative.begin() != "..";
}

GatherResult GatherParts(Assembly& assembly, const fs::path& from, const fs::path& to, const PartFolder& folder) {
    GatherResult result;
    std::map<std::string, fs::path> copies;   // source (settled) -> its copy; equal components share it
    // Parts already with the assembly: a copy among them with the same content is used again.
    std::vector<fs::path> with;
    for (const Component& c : assembly.components)
        if (const fs::path file = ResolvePartPath(from, c.path); LiesWith(to, file, folder.subfolders)) with.push_back(file);

    for (Component& c : assembly.components) {
        const fs::path source = ResolvePartPath(from, c.path);
        if (LiesWith(to, source, folder.subfolders)) {
            c.path = RelativePartPath(to, source);
            continue;
        }
        const std::string key = ToUtf8(Settled(source));
        if (const auto done = copies.find(key); done != copies.end()) {
            c.path = RelativePartPath(to, done->second);
            continue;
        }
        const std::optional<std::string> bytes = Bytes(source);
        if (!bytes || bytes->empty()) {
            // Not there (or empty): nothing to copy. The component keeps
            // pointing where it did; the assembly shows the part missing.
            result.problems.push_back(std::format("Деталь «{}» не найдена и не скопирована к сборке: {}",
                                                  ToUtf8(source.stem()), ToUtf8(source)));
            c.path = RelativePartPath(to, source);
            continue;
        }
        const std::string name = CopyName(source, to, from);
        fs::path copy;
        // A copy made at an earlier save ("Вал - Узел.bzc", "Вал - Узел (2).bzc"), unchanged since.
        const std::string copyStem = ToUtf8(PathFromUtf8(name).stem());
        for (const fs::path& f : with)
            if (const std::string n = ToUtf8(f.stem()); (n == copyStem || n.starts_with(copyStem + " (")) && Bytes(f) == bytes) {
                copy = f;
                break;
            }
        if (copy.empty()) {
            copy = folder.newFile ? folder.newFile(name) : fs::path{};
            bool done = !copy.empty();
            if (done) {
                std::ofstream out(copy, std::ios::binary | std::ios::trunc);
                out.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
                out.close();
                done = static_cast<bool>(out);
            }
            if (done && folder.written) done = folder.written(copy);
            if (!done) {
                result.ok = false;
                result.problems.push_back(std::format("Не удалось скопировать деталь «{}» в папку сборки", ToUtf8(source.stem())));
                c.path = RelativePartPath(to, source);
                continue;
            }
            result.copied.push_back(ToUtf8(copy.filename()));
            with.push_back(copy);
        }
        copies[key] = copy;
        c.path = RelativePartPath(to, copy);
    }
    assembly.Touch();
    return result;
}

} // namespace bz::assembly
