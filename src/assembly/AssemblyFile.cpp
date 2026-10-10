#include "assembly/AssemblyFile.h"

#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"
#include "document/JsonIO.h"

#include <array>
#include <format>
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

constexpr std::array<std::pair<ConstraintType, std::string_view>, 5> kTypes{{
    {ConstraintType::Mate, "mate"},
    {ConstraintType::Insert, "insert"},
    {ConstraintType::Angle, "angle"},
    {ConstraintType::Tangent, "tangent"},
    {ConstraintType::AxisMate, "axisMate"},
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
    return j;
}

bool Read(const Json& j, GeometryRef& out) {
    if (!j.is_object()) return false;
    GeometryRef g;
    const std::string kind = j.value("kind", "");
    if (!jio::FromId(kKinds, kind, g.kind)) return false;
    if (!ReadMember(j, "component", g.component) || !ReadMember(j, "origin", g.origin) ||
        !ReadMember(j, "direction", g.direction) || !ReadMember(j, "radius", g.radius) ||
        !ReadMember(j, "pick", g.pick))
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
        constraints.push_back(std::move(j));
    }
    Json root{
        {"assembly", {{"name", a.info.name}, {"designation", a.info.designation}}},
        {"formatVersion", kAssemblyFormatVersion},
        {"nextId", a.Ids().Last() + 1},
        {"components", std::move(components)},
        {"constraints", std::move(constraints)},
    };
    return root.dump(1, '\t');
}

Expected<Assembly> AssemblyFromJson(std::string_view text, std::vector<std::string>* warnings) {
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
            !Read(*first, c.first) || !Read(*second, c.second))
            return Error{"Повреждена зависимость сборки"};
        if (!a.FindComponent(c.first.component) || !a.FindComponent(c.second.component)) {
            if (warnings) warnings->push_back(std::format("Зависимость «{}» ссылается на удалённый компонент и пропущена", c.name));
            continue;
        }
        reserve(c.id);
        a.constraints.push_back(std::move(c));
    }

    if (std::uint64_t next = root.value("nextId", std::uint64_t{0}); next > 0) a.Ids().Reserve(ObjectId{next - 1});
    if (root.value("formatVersion", 0) > kAssemblyFormatVersion && warnings)
        warnings->push_back("Сборка создана более новой версией BreezeCAD. Новые возможности могут не открыться.");
    return a;
}

Status SaveAssembly(const Assembly& assembly, const std::filesystem::path& path, std::string_view generator) {
    return doc::WriteBzcArchive(path, doc::kAssemblyType, generator, AssemblyToJson(assembly));
}

Expected<Assembly> LoadAssembly(const std::filesystem::path& path, std::vector<std::string>* warnings) {
    Expected<doc::BzcArchive> archive = doc::ReadBzcArchive(path);
    if (!archive) return archive.GetError();
    if (archive->documentType != doc::kAssemblyType) {
        const doc::DocumentTypeInfo* type = doc::DocumentTypeRegistry::Default().Find(archive->documentType);
        return Error{std::format("Это не сборка, а документ «{}»", type ? type->title : archive->documentType)};
    }
    return AssemblyFromJson(archive->documentJson, warnings);
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

} // namespace bz::assembly
