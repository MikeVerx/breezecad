#pragma once
// Standard parts generator ("Библиотека"): fasteners, bearings and retaining
// rings by standard and size. Each part is built as a new document with an
// ordinary parametric tree (sketches with dimensions driven by variables,
// revolve or extrude, a cosmetic thread) and filled part properties, so it
// can be opened, changed and saved like any other part.
//
// The size tables are data (StandardParts.cpp). Geometry is simplified the
// usual way for a CAD library: no head chamfers, a socket or slot as a
// plain cut, a bearing as one body with grooves, a ring without lugs.

#include "core/Expected.h"
#include "document/Document.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bz::doc::parts {

enum class PartKind : unsigned char {
    HexBolt,            ///< болт с шестигранной головкой
    SocketScrew,        ///< винт с цилиндрической головкой и шестигранником
    CountersunkScrew,   ///< винт с потайной головкой
    PanScrew,           ///< винт с полукруглой головкой
    HexNut,             ///< гайка шестигранная
    LockNut,            ///< гайка самоконтрящаяся
    FlatWasher,         ///< шайба плоская (обычная и увеличенная)
    SpringWasher,       ///< шайба пружинная
    BallBearing,        ///< подшипник шариковый радиальный
    RingExternal,       ///< кольцо стопорное наружное (на вал)
    RingInternal,       ///< кольцо стопорное внутреннее (в отверстие)
};

struct KindInfo {
    PartKind         kind;
    std::string_view name;   ///< "Болт шестигранный" (Russian; translated when shown)
};

struct StandardInfo {
    std::string_view id;     ///< stable key, "gost7798"
    std::string_view title;  ///< "ГОСТ 7798-70"
    PartKind         kind;
    std::string_view note;   ///< what differs from others of the kind ("неполная резьба"), may be empty
};

/// Every kind, in the order the dialog lists them.
std::span<const KindInfo> Kinds();
/// Standards of one kind, in the order the dialog lists them.
std::vector<const StandardInfo*> StandardsOf(PartKind kind);
const StandardInfo* FindStandard(std::string_view id);

/// Sizes of a standard as shown: "M10", "6204", "30".
std::vector<std::string> Sizes(const StandardInfo& standard);
/// Lengths available for a size, mm; empty when the part has no length.
std::vector<double> Lengths(const StandardInfo& standard, std::string_view size);

struct Request {
    std::string standard;   ///< StandardInfo::id
    std::string size;       ///< one of Sizes()
    double      length = 0; ///< one of Lengths(), unused when there are none
};

/// Outer size of the part as the table gives it (mm): across X, Y and along
/// the axis Z. For tests and the dialog.
struct Envelope {
    double x = 0, y = 0, z = 0;
};

struct Made {
    Document doc;
    Envelope envelope;
};

/// Builds the part. Errors name an unknown standard, size or length.
Expected<Made> MakePart(const Request& request);

/// "Болт М10×40 ГОСТ 7798-70"; empty for an unknown request.
std::string Designation(const Request& request);

} // namespace bz::doc::parts
