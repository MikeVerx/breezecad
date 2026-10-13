#pragma once
// How bodies and faces look ("Внешний вид"): a colour with transparency,
// an edge colour and a kind of surface (matte, metal, glass). Purely
// visual: nothing here changes the geometry or the rebuild.
//
// A document keeps the appearances it uses (a copy of each), so a file
// shows the same on another computer whatever that one's library holds.

#include "core/Color.h"
#include "core/Id.h"
#include "document/FaceReference.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bz::doc {

enum class Finish : unsigned char {
    Matte,   ///< plastic, painted
    Metal,   ///< bright highlights
    Glass,   ///< see-through, glossy
};

struct Appearance {
    std::string id;     ///< "std.steel" for the standard ones, "user.<…>" for the user's own
    std::string name;   ///< shown in the library; the standard names are translated when shown
    Color       face = Color::Hex(0xC9CFD7);
    Color       edges = Color::Hex(0x3D4552);
    Finish      finish = Finish::Matte;
    /// A picture on the surface instead of a plain colour: its file name
    /// ("user.123.jpg"), the bytes are in Look::pictures or the library.
    /// It repeats edge to edge; one copy is `pictureSize` mm wide.
    std::string picture;
    double      pictureSize = 100.0;

    bool HasPicture() const { return !picture.empty(); }
    bool operator==(const Appearance&) const = default;
};

/// Which appearance goes where in one document.
struct Look {
    struct BodyLook {
        ObjectId    body;   ///< the feature that made the body (Body::id)
        std::string appearance;
        bool operator==(const BodyLook&) const = default;
    };
    struct FaceLook {
        ObjectId      body;
        FaceReference face;   ///< any face, found again by its point (curved too)
        std::string   appearance;
        bool operator==(const FaceLook&) const = default;
    };
    std::vector<Appearance> used;   ///< definitions of every appearance referred to below
    /// Bytes of the pictures those use, by file name (saved inside the .bzc).
    std::map<std::string, std::vector<std::uint8_t>> pictures;
    std::vector<BodyLook>   bodies;
    std::vector<FaceLook>   faces;

    const Appearance* Find(std::string_view id) const;
    /// The body's appearance, if one is set.
    const Appearance* OfBody(ObjectId body) const;
    /// Sets (or with an empty `appearance` removes) the whole body's look.
    /// Its faces' own looks go: the whole body was chosen.
    void SetBody(ObjectId body, const Appearance* appearance);
    /// Sets one face's look; a face already set nearby is replaced.
    void SetFace(ObjectId body, const FaceReference& face, const Appearance* appearance);
    /// Takes a changed definition of an appearance already in use; false if not in use.
    bool Refresh(const Appearance& appearance);
    /// Keeps only the definitions still in use.
    void Prune();
    bool Empty() const { return bodies.empty() && faces.empty(); }

    bool operator==(const Look&) const = default;
};

/// The library that comes with the program: metals, plastics, glass.
std::span<const Appearance> StandardAppearances();
const Appearance* FindStandard(std::string_view id);

nlohmann::json ToJson(const Appearance& a);
std::optional<Appearance> AppearanceFromJson(const nlohmann::json& j);
nlohmann::json ToJson(const Look& look);
Look LookFromJson(const nlohmann::json& j);

/// "#RRGGBB" or "#RRGGBBAA" (alpha when not opaque), and back.
std::string ColorToHex(const Color& c);
std::optional<Color> ColorFromHex(std::string_view text);

} // namespace bz::doc
