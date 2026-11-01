// DXF R12 writer laid out as AutoCAD expects it: HEADER, the full set of
// TABLES (VPORT, LTYPE, LAYER, STYLE, VIEW, UCS, APPID, DIMSTYLE), BLOCKS with
// $Model_Space / $Paper_Space, ENTITIES, every object with a handle. The
// earlier writer had a LAYER table pointing at a CONTINUOUS line type it never
// defined, no STYLE for its texts and Windows-1251 bytes in layer names:
// ezdxf read that, AutoCAD did not. The layout follows what ezdxf writes for
// R12 (ezdxf is checked against AutoCAD by its authors).

#include "sheetmetal/DxfDrawing.h"

#include <cmath>
#include <format>

namespace bz::dxf {
namespace {

class Out {
public:
    void Pair(int code, std::string_view value) {
        text += std::format("{:>3}\n", code);
        text += value;
        text += '\n';
    }
    void Pair(int code, const std::string& value) { Pair(code, std::string_view(value)); }
    void Pair(int code, const char* value) { Pair(code, std::string_view(value)); }
    void Pair(int code, int value) { Pair(code, std::to_string(value)); }
    void Pair(int code, double value) {
        std::string s = std::format("{:.10f}", value);
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (s.back() == '.') s += '0';
        if (s == "-0.0") s = "0.0";
        Pair(code, s);
    }
    void Point(int code, Vec2 p) {
        Pair(code, p.x);
        Pair(code + 10, p.y);
        Pair(code + 20, 0.0);
    }
    /// A new handle (hexadecimal, as DXF writes them).
    void Handle(int code = 5) { Pair(code, std::format("{:X}", next++)); }

    std::string text;
    unsigned    next = 0x20;
};

void Common(Out& o, const char* type, const Entity& e) {
    o.Pair(0, type);
    o.Handle();
    o.Pair(8, EscapeText(e.layer));
    if (e.color != 256) o.Pair(62, e.color);
}

void WriteEntity(Out& o, const Entity& e) {
    switch (e.kind) {
        case Kind::Line:
            Common(o, "LINE", e);
            o.Point(10, e.a);
            o.Point(11, e.b);
            return;
        case Kind::Circle:
            Common(o, "CIRCLE", e);
            o.Point(10, e.center);
            o.Pair(40, e.radius);
            return;
        case Kind::Arc:
            Common(o, "ARC", e);
            o.Point(10, e.center);
            o.Pair(40, e.radius);
            o.Pair(50, e.startDeg);
            o.Pair(51, e.endDeg);
            return;
        case Kind::Polyline: {
            Common(o, "POLYLINE", e);
            o.Pair(66, 1);
            o.Point(10, {});
            o.Pair(70, e.closed ? 1 : 0);
            for (std::size_t i = 0; i < e.points.size(); ++i) {
                o.Pair(0, "VERTEX");
                o.Handle();
                o.Pair(8, EscapeText(e.layer));
                o.Point(10, e.points[i]);
                if (i < e.bulges.size() && e.bulges[i] != 0.0) o.Pair(42, e.bulges[i]);
            }
            o.Pair(0, "SEQEND");
            o.Handle();
            o.Pair(8, EscapeText(e.layer));
            return;
        }
        case Kind::Text: {
            // A DXF value is one line: a text of several lines becomes a TEXT per line, one under another.
            const double a = e.rotationDeg * kPi / 180.0;
            const Vec2 down{std::sin(a), -std::cos(a)};
            std::size_t from = 0;
            for (int line = 0;; ++line) {
                const std::size_t to = e.text.find('\n', from);
                std::string piece = e.text.substr(from, to == std::string::npos ? std::string::npos : to - from);
                for (char& ch : piece)
                    if (static_cast<unsigned char>(ch) < 0x20) ch = ' ';   // tabs, carriage returns
                if (!piece.empty()) {
                    Common(o, "TEXT", e);
                    o.Point(10, e.at + down * (e.height * 1.6 * line));
                    o.Pair(40, e.height);
                    o.Pair(1, EscapeText(piece));
                    if (e.rotationDeg != 0.0) o.Pair(50, e.rotationDeg);
                    o.Pair(7, "Standard");
                }
                if (to == std::string::npos) break;
                from = to + 1;
            }
            return;
        }
    }
}

void Table(Out& o, const char* name, int count) {
    o.Pair(0, "TABLE");
    o.Pair(2, name);
    o.Pair(70, count);
}

} // namespace

// --- entities and drawings ---------------------------------------------------------

Entity Entity::Line(Vec2 a, Vec2 b, std::string layer) {
    Entity e;
    e.kind = Kind::Line, e.a = a, e.b = b, e.layer = std::move(layer);
    return e;
}
Entity Entity::Arc(Vec2 center, double radius, double startDeg, double endDeg, std::string layer) {
    Entity e;
    e.kind = Kind::Arc, e.center = center, e.radius = radius, e.startDeg = startDeg, e.endDeg = endDeg, e.layer = std::move(layer);
    return e;
}
Entity Entity::Circle(Vec2 center, double radius, std::string layer) {
    Entity e;
    e.kind = Kind::Circle, e.center = center, e.radius = radius, e.layer = std::move(layer);
    return e;
}
Entity Entity::Text(Vec2 at, double height, double rotationDeg, std::string text, std::string layer) {
    Entity e;
    e.kind = Kind::Text, e.at = at, e.height = height, e.rotationDeg = rotationDeg, e.text = std::move(text), e.layer = std::move(layer);
    return e;
}

const Layer* Drawing::FindLayer(std::string_view name) const {
    for (const Layer& l : layers)
        if (l.name == name) return &l;
    return nullptr;
}
Layer* Drawing::FindLayer(std::string_view name) {
    for (Layer& l : layers)
        if (l.name == name) return &l;
    return nullptr;
}
Layer& Drawing::EnsureLayer(std::string_view name, int color) {
    if (Layer* l = FindLayer(name)) return *l;
    layers.push_back({std::string(name), color, true});
    return layers.back();
}

// --- text ------------------------------------------------------------------------------

std::string EscapeText(std::string_view utf8) {
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned cp = '?';
        std::size_t len = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 0x6 && i + 1 < utf8.size()) cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu), len = 2;
        else if ((c >> 4) == 0xE && i + 2 < utf8.size())
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu), len = 3;
        else if ((c >> 3) == 0x1E) len = 4;
        i += len;
        if (cp == 0xB0) out += "%%d";   // °: AutoCAD's own code, every CAM program knows it
        else if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp <= 0xFFFF) out += std::format("\\U+{:04X}", cp);
        else out += '?';
    }
    return out;
}

// --- R12 ---------------------------------------------------------------------------------

std::string WriteR12(const Drawing& drawing) {
    Out o;
    // Layers: "0" first, then the drawing's, then any an entity names without a table entry.
    std::vector<Layer> layers{{"0", 7, true}};
    const auto add = [&](const std::string& name, int color, bool visible) {
        for (const Layer& l : layers)
            if (l.name == name) return;
        layers.push_back({name, color, visible});
    };
    for (const Layer& l : drawing.layers) add(l.name, l.color, l.visible);
    for (const Entity& e : drawing.entities) add(e.layer, 7, true);

    Box2 box = Bounds(drawing);
    if (box.IsEmpty()) box.Include(Vec2{}), box.Include(Vec2{100, 100});

    o.Pair(0, "SECTION");
    o.Pair(2, "HEADER");
    o.Pair(9, "$ACADVER"), o.Pair(1, "AC1009");
    o.Pair(9, "$DWGCODEPAGE"), o.Pair(3, "ANSI_1252");
    o.Pair(9, "$INSBASE"), o.Point(10, {});
    o.Pair(9, "$EXTMIN"), o.Point(10, box.min);
    o.Pair(9, "$EXTMAX"), o.Point(10, box.max);
    o.Pair(9, "$LIMMIN"), o.Pair(10, 0.0), o.Pair(20, 0.0);
    o.Pair(9, "$LIMMAX"), o.Pair(10, 420.0), o.Pair(20, 297.0);
    o.Pair(9, "$TEXTSIZE"), o.Pair(40, 2.5);
    o.Pair(9, "$TEXTSTYLE"), o.Pair(7, "Standard");
    o.Pair(9, "$CLAYER"), o.Pair(8, "0");
    o.Pair(9, "$CELTYPE"), o.Pair(6, "ByLayer");
    o.Pair(9, "$LUNITS"), o.Pair(70, 2);
    o.Pair(9, "$LUPREC"), o.Pair(70, 4);
    o.Pair(9, "$HANDLING"), o.Pair(70, 1);
    const std::size_t seedAt = o.text.size();   // $HANDSEED goes here once the handles are known
    o.Pair(0, "ENDSEC");
    const std::string header = o.text.substr(0, seedAt);
    const std::string headerEnd = o.text.substr(seedAt);
    o.text.clear();

    o.Pair(0, "SECTION");
    o.Pair(2, "TABLES");

    Table(o, "VPORT", 1);
    o.Pair(0, "VPORT");
    o.Handle();
    o.Pair(2, "*Active");
    o.Pair(70, 0);
    o.Pair(10, 0.0), o.Pair(20, 0.0);
    o.Pair(11, 1.0), o.Pair(21, 1.0);
    const Vec2 middle = (box.min + box.max) * 0.5;
    o.Pair(12, middle.x), o.Pair(22, middle.y);
    o.Pair(13, 0.0), o.Pair(23, 0.0);
    o.Pair(14, 0.5), o.Pair(24, 0.5);
    o.Pair(15, 0.5), o.Pair(25, 0.5);
    o.Pair(16, 0.0), o.Pair(26, 0.0), o.Pair(36, 1.0);
    o.Pair(17, 0.0), o.Pair(27, 0.0), o.Pair(37, 0.0);
    o.Pair(40, std::max(box.max.y - box.min.y, box.max.x - box.min.x) * 1.2);
    o.Pair(41, 1.34);
    o.Pair(42, 50.0);
    o.Pair(43, 0.0), o.Pair(44, 0.0), o.Pair(50, 0.0), o.Pair(51, 0.0);
    o.Pair(71, 0), o.Pair(72, 1000), o.Pair(73, 1), o.Pair(74, 3), o.Pair(75, 0), o.Pair(76, 0), o.Pair(77, 0), o.Pair(78, 0);
    o.Pair(0, "ENDTAB");

    Table(o, "LTYPE", 3);
    for (const char* name : {"ByBlock", "ByLayer", "Continuous"}) {
        o.Pair(0, "LTYPE");
        o.Handle();
        o.Pair(2, name);
        o.Pair(70, 0);
        o.Pair(3, std::string_view(std::string_view(name) == "Continuous" ? "Solid line" : ""));
        o.Pair(72, 65);
        o.Pair(73, 0);
        o.Pair(40, 0.0);
    }
    o.Pair(0, "ENDTAB");

    Table(o, "LAYER", static_cast<int>(layers.size()));
    for (const Layer& l : layers) {
        o.Pair(0, "LAYER");
        o.Handle();
        o.Pair(2, EscapeText(l.name));
        o.Pair(70, 0);
        o.Pair(62, l.visible ? l.color : -l.color);   // a negative colour: the layer is off
        o.Pair(6, "Continuous");
    }
    o.Pair(0, "ENDTAB");

    Table(o, "STYLE", 1);
    o.Pair(0, "STYLE");
    o.Handle();
    o.Pair(2, "Standard");
    o.Pair(70, 0);
    o.Pair(40, 0.0), o.Pair(41, 1.0), o.Pair(50, 0.0);
    o.Pair(71, 0);
    o.Pair(42, 2.5);
    o.Pair(3, "txt");
    o.Pair(4, "");
    o.Pair(0, "ENDTAB");

    Table(o, "VIEW", 0);
    o.Pair(0, "ENDTAB");
    Table(o, "UCS", 0);
    o.Pair(0, "ENDTAB");

    Table(o, "APPID", 1);
    o.Pair(0, "APPID");
    o.Handle();
    o.Pair(2, "ACAD");
    o.Pair(70, 0);
    o.Pair(0, "ENDTAB");

    Table(o, "DIMSTYLE", 1);
    o.Pair(0, "DIMSTYLE");
    o.Handle(105);
    o.Pair(2, "Standard");
    o.Pair(70, 0);
    for (int code : {3, 4, 5, 6, 7}) o.Pair(code, "");
    o.Pair(40, 1.0), o.Pair(41, 2.5), o.Pair(42, 0.625), o.Pair(43, 3.75), o.Pair(44, 1.25);
    o.Pair(45, 0.0), o.Pair(46, 0.0), o.Pair(47, 0.0), o.Pair(48, 0.0);
    o.Pair(140, 2.5), o.Pair(141, 2.5), o.Pair(142, 0.0), o.Pair(143, 0.03937007874), o.Pair(144, 1.0);
    o.Pair(145, 0.0), o.Pair(146, 1.0), o.Pair(147, 0.625);
    o.Pair(71, 0), o.Pair(72, 0), o.Pair(73, 0), o.Pair(74, 0), o.Pair(75, 0), o.Pair(76, 0), o.Pair(77, 1), o.Pair(78, 8);
    o.Pair(170, 0), o.Pair(171, 3), o.Pair(172, 1), o.Pair(173, 0), o.Pair(174, 0), o.Pair(175, 0), o.Pair(176, 0), o.Pair(177, 0), o.Pair(178, 0);
    o.Pair(0, "ENDTAB");
    o.Pair(0, "ENDSEC");

    o.Pair(0, "SECTION");
    o.Pair(2, "BLOCKS");
    for (const char* name : {"$Model_Space", "$Paper_Space"}) {
        o.Pair(0, "BLOCK");
        o.Handle();
        o.Pair(8, "0");
        o.Pair(2, name);
        o.Pair(70, 0);
        o.Point(10, {});
        o.Pair(3, name);
        o.Pair(1, "");
        o.Pair(0, "ENDBLK");
        o.Handle();
        o.Pair(8, "0");
    }
    o.Pair(0, "ENDSEC");

    o.Pair(0, "SECTION");
    o.Pair(2, "ENTITIES");
    for (const Entity& e : drawing.entities) WriteEntity(o, e);
    o.Pair(0, "ENDSEC");
    o.Pair(0, "EOF");

    Out seed;
    seed.Pair(9, "$HANDSEED");
    seed.Pair(5, std::format("{:X}", o.next));
    return header + seed.text + headerEnd + o.text;
}

} // namespace bz::dxf
