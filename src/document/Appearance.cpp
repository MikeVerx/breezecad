#include "document/Appearance.h"

#include "core/I18n.h"
#include "document/JsonIO.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>

namespace bz::doc {

using json_io::Json;

namespace {

Appearance Make(const char* id, const char* name, std::uint32_t face, std::uint32_t edges, Finish finish, float alpha = 1.0f) {
    return {id, name, Color::Hex(face, alpha), Color::Hex(edges), finish};
}

const std::vector<Appearance>& Library() {
    static const std::vector<Appearance> library{
        // Metals: the colour of the metal, bright highlights.
        Make("std.steel", N_("Сталь"), 0xB9C0C8, 0x3D4552, Finish::Metal),
        Make("std.stainless", N_("Нержавеющая сталь"), 0xD3D8DE, 0x4A525E, Finish::Metal),
        Make("std.aluminium", N_("Алюминий"), 0xDCE1E6, 0x5A626E, Finish::Metal),
        Make("std.blued", N_("Воронёная сталь"), 0x2F3742, 0x0F1114, Finish::Metal),
        Make("std.castiron", N_("Чугун"), 0x5C6066, 0x24272B, Finish::Matte),
        Make("std.titanium", N_("Титан"), 0xA9A79F, 0x45443F, Finish::Metal),
        Make("std.copper", N_("Медь"), 0xC77845, 0x5A2E14, Finish::Metal),
        Make("std.brass", N_("Латунь"), 0xCDA955, 0x5E4A16, Finish::Metal),
        Make("std.gold", N_("Золото"), 0xE6BC48, 0x6B5214, Finish::Metal),
        Make("std.silver", N_("Серебро"), 0xE7E9EC, 0x6A727E, Finish::Metal),
        // Polycarbonate: see-through.
        Make("std.pc.clear", N_("Поликарбонат прозрачный"), 0xE6F0F8, 0x7D8C99, Finish::Glass, 0.28f),
        Make("std.pc.smoked", N_("Поликарбонат дымчатый"), 0x464C54, 0x1B1F24, Finish::Glass, 0.55f),
        // Plastics.
        Make("std.plastic.white", N_("Пластик белый"), 0xF1F1EE, 0x6A727E, Finish::Matte),
        Make("std.plastic.black", N_("Пластик чёрный"), 0x2A2C2F, 0x0F1114, Finish::Matte),
        Make("std.plastic.grey", N_("Пластик серый"), 0x8E959D, 0x2E333A, Finish::Matte),
        Make("std.plastic.red", N_("Пластик красный"), 0xD13B32, 0x5C130F, Finish::Matte),
        Make("std.plastic.orange", N_("Пластик оранжевый"), 0xEE7E2B, 0x6B320A, Finish::Matte),
        Make("std.plastic.yellow", N_("Пластик жёлтый"), 0xF2C431, 0x6B520A, Finish::Matte),
        Make("std.plastic.green", N_("Пластик зелёный"), 0x3F9C50, 0x15401C, Finish::Matte),
        Make("std.plastic.cyan", N_("Пластик голубой"), 0x5CB7E8, 0x184C6B, Finish::Matte),
        Make("std.plastic.blue", N_("Пластик синий"), 0x2F5CD0, 0x10245C, Finish::Matte),
        Make("std.plastic.violet", N_("Пластик фиолетовый"), 0x7B50C4, 0x2E1755, Finish::Matte),
        Make("std.plastic.pink", N_("Пластик розовый"), 0xEE8CB5, 0x6B2443, Finish::Matte),
    };
    return library;
}

const char* FinishId(Finish f) { return f == Finish::Metal ? "metal" : f == Finish::Glass ? "glass" : "matte"; }

/// Adds the definition, or brings the one kept up to date.
void Remember(std::vector<Appearance>& used, const Appearance& a) {
    const auto it = std::ranges::find(used, a.id, &Appearance::id);
    if (it != used.end()) *it = a;
    else used.push_back(a);
}

/// Faces closer than this (mm) are the same face picked twice.
constexpr double kSameFace = 1e-3;

} // namespace

std::span<const Appearance> StandardAppearances() { return Library(); }

const Appearance* FindStandard(std::string_view id) {
    const auto& lib = Library();
    const auto it = std::ranges::find(lib, id, &Appearance::id);
    return it != lib.end() ? &*it : nullptr;
}

const Appearance* Look::Find(std::string_view id) const {
    const auto it = std::ranges::find(used, id, &Appearance::id);
    return it != used.end() ? &*it : nullptr;
}

const Appearance* Look::OfBody(ObjectId body) const {
    const auto it = std::ranges::find(bodies, body, &BodyLook::body);
    return it != bodies.end() ? Find(it->appearance) : nullptr;
}

void Look::SetBody(ObjectId body, const Appearance* appearance) {
    std::erase_if(bodies, [&](const BodyLook& b) { return b.body == body; });
    std::erase_if(faces, [&](const FaceLook& f) { return f.body == body; });
    if (appearance) {
        Remember(used, *appearance);
        bodies.push_back({body, appearance->id});
    }
    Prune();
}

void Look::SetFace(ObjectId body, const FaceReference& face, const Appearance* appearance) {
    std::erase_if(faces, [&](const FaceLook& f) { return f.body == body && (f.face.point - face.point).Length() < kSameFace; });
    if (appearance) {
        Remember(used, *appearance);
        faces.push_back({body, face, appearance->id});
    }
    Prune();
}

bool Look::Refresh(const Appearance& appearance) {
    if (!Find(appearance.id)) return false;
    Remember(used, appearance);
    return true;
}

void Look::Prune() {
    std::erase_if(used, [&](const Appearance& a) {
        return std::ranges::none_of(bodies, [&](const BodyLook& b) { return b.appearance == a.id; }) &&
               std::ranges::none_of(faces, [&](const FaceLook& f) { return f.appearance == a.id; });
    });
    std::erase_if(pictures, [&](const auto& p) { return std::ranges::none_of(used, [&](const Appearance& a) { return a.picture == p.first; }); });
}

std::string ColorToHex(const Color& c) {
    const auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::string out = std::format("#{:02X}{:02X}{:02X}", byte(c.r), byte(c.g), byte(c.b));
    if (byte(c.a) != 255) out += std::format("{:02X}", byte(c.a));
    return out;
}

std::optional<Color> ColorFromHex(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8) return std::nullopt;
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    if (text.size() == 6) return Color::Hex(value);
    return Color::Hex(value >> 8, static_cast<float>(value & 0xFF) / 255.0f);
}

Json ToJson(const Appearance& a) {
    Json j{{"id", a.id}, {"name", a.name}, {"face", ColorToHex(a.face)}, {"edges", ColorToHex(a.edges)}, {"finish", FinishId(a.finish)}};
    if (a.HasPicture()) {
        j["picture"] = a.picture;
        j["pictureSize"] = a.pictureSize;
    }
    return j;
}

std::optional<Appearance> AppearanceFromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    Appearance a;
    a.id = j.value("id", "");
    a.name = j.value("name", "");
    const auto face = ColorFromHex(j.value("face", ""));
    const auto edges = ColorFromHex(j.value("edges", ""));
    if (a.id.empty() || !face || !edges) return std::nullopt;
    a.face = *face;
    a.edges = *edges;
    const std::string finish = j.value("finish", "matte");
    a.finish = finish == "metal" ? Finish::Metal : finish == "glass" ? Finish::Glass : Finish::Matte;
    a.picture = j.value("picture", "");
    // A picture name is a plain file name: never a path out of its folder.
    if (a.picture.find_first_of("/\\") != std::string::npos || a.picture.starts_with(".")) a.picture.clear();
    a.pictureSize = std::max(0.01, j.value("pictureSize", 100.0));
    return a;
}

Json ToJson(const Look& look) {
    Json used = Json::array(), bodies = Json::array(), faces = Json::array();
    for (const Appearance& a : look.used) used.push_back(ToJson(a));
    for (const auto& b : look.bodies) bodies.push_back({{"body", b.body.value}, {"appearance", b.appearance}});
    for (const auto& f : look.faces) faces.push_back({{"body", f.body.value}, {"face", json_io::ToJson(f.face)}, {"appearance", f.appearance}});
    return Json{{"appearances", std::move(used)}, {"bodies", std::move(bodies)}, {"faces", std::move(faces)}};
}

Look LookFromJson(const Json& j) {
    Look look;
    if (!j.is_object()) return look;
    // Unreadable entries are skipped: looks are never worth refusing a file over.
    for (const Json& a : j.value("appearances", Json::array()))
        if (auto parsed = AppearanceFromJson(a)) look.used.push_back(std::move(*parsed));
    for (const Json& b : j.value("bodies", Json::array())) {
        Look::BodyLook body;
        if (!json_io::ReadMember(b, "body", body.body) || !b.contains("appearance") || !b["appearance"].is_string()) continue;
        body.appearance = b["appearance"].get<std::string>();
        if (look.Find(body.appearance)) look.bodies.push_back(std::move(body));
    }
    for (const Json& f : j.value("faces", Json::array())) {
        Look::FaceLook face;
        if (!json_io::ReadMember(f, "body", face.body) || !f.contains("face") || !json_io::Read(f["face"], face.face) ||
            !f.contains("appearance") || !f["appearance"].is_string())
            continue;
        face.appearance = f["appearance"].get<std::string>();
        if (look.Find(face.appearance)) look.faces.push_back(std::move(face));
    }
    return look;
}

} // namespace bz::doc
