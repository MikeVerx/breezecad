// DXF reader for the DXF editor: ASCII DXF of any version. Entities the
// editor does not show (hatches, dimensions, 3D) are counted and skipped.

#include "sheetmetal/DxfDrawing.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <optional>

namespace bz::dxf {
namespace {

struct Pair {
    int              code = 0;
    std::string_view value;
};

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

std::string_view CutCr(std::string_view s) {
    if (!s.empty() && s.back() == '\r') s.remove_suffix(1);
    return s;
}

/// A number as DXF writes it ("-12.5", "1.0E+02"; "+1", "1,5" from careless
/// writers). Parsed by hand: floating-point std::from_chars is missing from the
/// Android and older Apple C++ libraries, and strtod follows the C locale.
double Number(std::string_view s) {
    s = Trim(s);
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) negative = s[i++] == '-';
    double v = 0.0;
    bool digits = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') v = v * 10.0 + (s[i++] - '0'), digits = true;
    if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
        ++i;
        double scale = 0.1;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') v += (s[i++] - '0') * scale, scale *= 0.1, digits = true;
    }
    if (!digits) return 0.0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool negativeExp = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) negativeExp = s[i++] == '-';
        int e = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && e < 400) e = e * 10 + (s[i++] - '0');
        v *= std::pow(10.0, negativeExp ? -e : e);
    }
    return negative ? -v : v;
}

int Integer(std::string_view s) { return static_cast<int>(std::lround(Number(s))); }

void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) out += static_cast<char>(0xC0 | (cp >> 6)), out += static_cast<char>(0x80 | (cp & 0x3F));
    else out += static_cast<char>(0xE0 | (cp >> 12)), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
}

bool ValidUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) return false;
        i += len;
    }
    return true;
}

enum class CodePage { Utf8, Cp1251, Cp1252 };

/// Bytes of a string value as UTF-8.
std::string Decode(std::string_view raw, CodePage page) {
    if (page == CodePage::Utf8 || ValidUtf8(raw)) return std::string(raw);
    std::string out;
    for (const char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) out += ch;
        else if (page == CodePage::Cp1251) {
            if (c >= 0xC0) AppendUtf8(out, 0x410 + (c - 0xC0));
            else if (c == 0xA8) AppendUtf8(out, 0x401);
            else if (c == 0xB8) AppendUtf8(out, 0x451);
            else if (c == 0xB9) AppendUtf8(out, 0x2116);
            else AppendUtf8(out, c);   // °, ±, « » and the like share their code with Latin-1
        } else {
            AppendUtf8(out, c);
        }
    }
    return out;
}

/// MTEXT without its formatting: \P is a new line, {\f…;} and the like go.
std::string PlainMText(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '{' || c == '}') continue;
        if (c != '\\' || i + 1 >= s.size()) {
            out += c;
            continue;
        }
        const char k = s[i + 1];
        if (k == 'P' || k == 'n') {
            out += '\n';
            ++i;
        } else if (k == '\\' || k == '{' || k == '}') {
            out += k;
            ++i;
        } else if (k == 'U' || k == 'u') {
            out += c;   // \U+XXXX: UnescapeText handles it
        } else if (std::string_view("LlOoKkNX~").find(k) != std::string_view::npos) {
            if (k == '~') out += ' ';
            ++i;
        } else if (std::string_view("fFHWQTACcpS").find(k) != std::string_view::npos) {
            const std::size_t end = s.find(';', i);   // \fArial|b0|i0;  \H2.5;
            i = end == std::string::npos ? s.size() : end;
        } else {
            out += c;
        }
    }
    return out;
}

/// A B-spline (NURBS when `weights` are given) sampled into points, de Boor.
std::vector<Vec2> SplinePoints(int degree, const std::vector<double>& knots, const std::vector<Vec2>& ctrl,
                               const std::vector<double>& weights) {
    std::vector<Vec2> out;
    const int n = static_cast<int>(ctrl.size());
    if (degree < 1 || n <= degree || static_cast<int>(knots.size()) != n + degree + 1) return ctrl;
    const double t0 = knots[static_cast<std::size_t>(degree)], t1 = knots[static_cast<std::size_t>(n)];
    const int samples = std::max(16, n * 8);
    const bool rational = weights.size() == ctrl.size();
    for (int s = 0; s <= samples; ++s) {
        const double t = s == samples ? t1 : t0 + (t1 - t0) * s / samples;
        int k = degree;
        while (k < n - 1 && t >= knots[static_cast<std::size_t>(k + 1)]) ++k;
        std::vector<Vec3> d(static_cast<std::size_t>(degree + 1));   // x*w, y*w, w
        for (int j = 0; j <= degree; ++j) {
            const std::size_t i = static_cast<std::size_t>(j + k - degree);
            const double w = rational ? weights[i] : 1.0;
            d[static_cast<std::size_t>(j)] = {ctrl[i].x * w, ctrl[i].y * w, w};
        }
        for (int r = 1; r <= degree; ++r)
            for (int j = degree; j >= r; --j) {
                const std::size_t i = static_cast<std::size_t>(j + k - degree);
                const double den = knots[i + static_cast<std::size_t>(degree - r + 1)] - knots[i];
                const double a = den > 0 ? (t - knots[i]) / den : 0.0;
                d[static_cast<std::size_t>(j)] = d[static_cast<std::size_t>(j - 1)] * (1 - a) + d[static_cast<std::size_t>(j)] * a;
            }
        const Vec3 p = d[static_cast<std::size_t>(degree)];
        out.push_back(p.z != 0 ? Vec2{p.x / p.z, p.y / p.z} : Vec2{p.x, p.y});
    }
    return out;
}

/// A smooth curve through `fit` (a spline given only by the points it passes):
/// centripetal Catmull-Rom. AutoCAD's own curve through the same points is a
/// cubic B-spline; the two differ by a little between the points.
std::vector<Vec2> ThroughPoints(const std::vector<Vec2>& fit, bool closed) {
    const std::size_t n = fit.size();
    if (n < 3) return fit;
    const auto at = [&](long i) {
        if (closed) return fit[static_cast<std::size_t>((i % static_cast<long>(n) + static_cast<long>(n)) % static_cast<long>(n))];
        if (i < 0) return fit[0] * 2.0 - fit[1];
        if (i >= static_cast<long>(n)) return fit[n - 1] * 2.0 - fit[n - 2];
        return fit[static_cast<std::size_t>(i)];
    };
    std::vector<Vec2> out;
    const long segments = closed ? static_cast<long>(n) : static_cast<long>(n) - 1;
    for (long i = 0; i < segments; ++i) {
        const Vec2 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        const auto knot = [](double t, Vec2 a, Vec2 b) { return t + std::sqrt(std::max((b - a).Length(), 1e-12)); };
        const double t0 = 0, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
        const int steps = 12;
        for (int k = i == 0 ? 0 : 1; k <= steps; ++k) {
            const double t = t1 + (t2 - t1) * k / steps;
            const Vec2 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
            const Vec2 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
            const Vec2 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
            const Vec2 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
            const Vec2 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
            out.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
        }
    }
    if (closed && !out.empty()) out.pop_back();
    return out;
}

Entity Polyline(std::vector<Vec2> pts, bool closed, std::string layer, int color) {
    Entity e;
    e.kind = Kind::Polyline;
    e.points = std::move(pts);
    e.bulges.assign(e.points.size(), 0.0);
    e.closed = closed;
    e.layer = std::move(layer);
    e.color = color;
    return e;
}

/// Where a block reference puts the block's geometry.
struct Placement {
    Vec2   base, at;
    double sx = 1, sy = 1, rotDeg = 0;
    Vec2 Apply(Vec2 p) const {
        const Vec2 q{(p.x - base.x) * sx, (p.y - base.y) * sy};
        const double a = rotDeg * kPi / 180.0;
        return at + Vec2{q.x * std::cos(a) - q.y * std::sin(a), q.x * std::sin(a) + q.y * std::cos(a)};
    }
};

void Place(Entity& e, const Placement& p) {
    const bool uniform = std::abs(std::abs(p.sx) - std::abs(p.sy)) < 1e-9;
    const bool mirrored = p.sx * p.sy < 0;
    if ((e.kind == Kind::Arc || e.kind == Kind::Circle) && (!uniform || (mirrored && e.kind == Kind::Arc))) {
        // Turned into a polyline: an arc does not stay an arc under these.
        Entity poly = Polyline(Outline(e, 3.0), e.kind == Kind::Circle, e.layer, e.color);
        if (poly.closed && poly.points.size() > 1) poly.points.pop_back(), poly.bulges.pop_back();
        e = std::move(poly);
    }
    switch (e.kind) {
        case Kind::Line: e.a = p.Apply(e.a), e.b = p.Apply(e.b); break;
        case Kind::Circle: e.center = p.Apply(e.center), e.radius *= std::abs(p.sx); break;
        case Kind::Arc:
            e.center = p.Apply(e.center), e.radius *= std::abs(p.sx);
            e.startDeg += p.rotDeg, e.endDeg += p.rotDeg;
            break;
        case Kind::Polyline:
            for (Vec2& q : e.points) q = p.Apply(q);
            if (mirrored)
                for (double& b : e.bulges) b = -b;
            break;
        case Kind::Text:
            e.at = p.Apply(e.at), e.height *= std::abs(p.sy), e.rotationDeg += p.rotDeg;
            break;
    }
}

class Reader {
public:
    Reader(std::string_view text, ReadReport& report) : myReport(report) {
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t e1 = text.find('\n', pos);
            if (e1 == std::string_view::npos) break;
            const std::string_view codeLine = Trim(text.substr(pos, e1 - pos));
            const std::size_t e2 = text.find('\n', e1 + 1);
            const std::string_view value = CutCr(text.substr(e1 + 1, (e2 == std::string_view::npos ? text.size() : e2) - e1 - 1));
            int code = 0;
            if (std::from_chars(codeLine.data(), codeLine.data() + codeLine.size(), code).ec != std::errc{}) {
                myBad = true;
                break;
            }
            myPairs.push_back({code, value});
            if (e2 == std::string_view::npos) break;
            pos = e2 + 1;
        }
    }

    Expected<Drawing> Run() {
        if (myBad || myPairs.empty()) return Error{"Файл не похож на DXF (текстовый DXF ожидался)"};
        // HEADER first: the code page and units decide how the rest reads.
        for (std::size_t i = 0; i + 1 < myPairs.size(); ++i) {
            if (myPairs[i].code != 9) continue;
            const std::string_view name = Trim(myPairs[i].value), v = Trim(myPairs[i + 1].value);
            if (name == "$ACADVER") {
                myReport.version = std::string(v);
                if (v >= "AC1021") myPage = CodePage::Utf8;   // 2007 and later: UTF-8
            } else if (name == "$DWGCODEPAGE" && myPage != CodePage::Utf8) {
                myPage = v.find("1251") != std::string_view::npos ? CodePage::Cp1251 : CodePage::Cp1252;
            } else if (name == "$INSUNITS") {
                switch (Integer(v)) {
                    case 1: myScale = 25.4; break;
                    case 2: myScale = 304.8; break;
                    case 5: myScale = 10.0; break;
                    case 6: myScale = 1000.0; break;
                    default: myScale = 1.0; break;
                }
            }
        }
        myReport.unitsToMm = myScale;
        // Sections.
        for (std::size_t i = 0; i + 1 < myPairs.size(); ++i) {
            if (myPairs[i].code != 0 || Trim(myPairs[i].value) != "SECTION" || myPairs[i + 1].code != 2) continue;
            const std::string_view name = Trim(myPairs[i + 1].value);
            std::size_t end = i + 2;
            while (end < myPairs.size() && !(myPairs[end].code == 0 && Trim(myPairs[end].value) == "ENDSEC")) ++end;
            if (name == "TABLES") ReadLayers(i + 2, end);
            else if (name == "BLOCKS") ReadBlocks(i + 2, end);
            else if (name == "ENTITIES") myEntitiesAt = {i + 2, end};
            i = end;
        }
        if (!myEntitiesAt) return Error{"В файле нет раздела ENTITIES — это не чертёж"};
        ReadEntities(myEntitiesAt->first, myEntitiesAt->second, nullptr, 0, myDrawing.entities);
        for (const Entity& e : myDrawing.entities)
            if (!myDrawing.FindLayer(e.layer)) myDrawing.layers.push_back({e.layer, 7, true});
        for (const auto& [kind, count] : mySkipped) myReport.skipped.emplace_back(kind, count);
        return std::move(myDrawing);
    }

private:
    std::string Text(std::string_view raw) const { return Decode(raw, myPage); }
    double Len(std::string_view v) const { return Number(v) * myScale; }

    void ReadLayers(std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
            if (myPairs[i].code != 0 || Trim(myPairs[i].value) != "LAYER") continue;
            Layer l;
            int flags = 0;
            for (std::size_t k = i + 1; k < to && myPairs[k].code != 0; ++k) {
                if (myPairs[k].code == 2) l.name = UnescapeText(Text(Trim(myPairs[k].value)));
                else if (myPairs[k].code == 62) l.color = Integer(myPairs[k].value);
                else if (myPairs[k].code == 70) flags = Integer(myPairs[k].value);
            }
            if (l.name.empty()) continue;
            l.visible = l.color >= 0 && (flags & 1) == 0;
            l.color = std::abs(l.color);
            if (l.color == 0 || l.color > 255) l.color = 7;
            if (!myDrawing.FindLayer(l.name)) myDrawing.layers.push_back(l);
        }
    }

    void ReadBlocks(std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
            if (myPairs[i].code != 0 || Trim(myPairs[i].value) != "BLOCK") continue;
            std::string name;
            Vec2 base;
            std::size_t k = i + 1;
            for (; k < to && myPairs[k].code != 0; ++k) {
                if (myPairs[k].code == 2) name = Text(Trim(myPairs[k].value));
                else if (myPairs[k].code == 10) base.x = Len(myPairs[k].value);
                else if (myPairs[k].code == 20) base.y = Len(myPairs[k].value);
            }
            std::size_t end = k;
            while (end < to && !(myPairs[end].code == 0 && Trim(myPairs[end].value) == "ENDBLK")) ++end;
            myBlocks[name] = {base, k, end};
            i = end;
        }
    }

    /// Entities in [from, to) into `out`; `place` applies a block reference.
    void ReadEntities(std::size_t from, std::size_t to, const Placement* place, int depth, std::vector<Entity>& out) {
        std::size_t i = from;
        while (i < to) {
            if (myPairs[i].code != 0) {
                ++i;
                continue;
            }
            const std::string_view type = Trim(myPairs[i].value);
            std::size_t end = i + 1;
            while (end < to && myPairs[end].code != 0) ++end;
            const std::size_t first = i + 1;
            i = end;
            if (type == "SEQEND" || type == "VERTEX" || type == "ATTRIB" || type == "ENDBLK") continue;
            if (Paper(first, end)) {
                if (type == "POLYLINE") i = SkipSequence(end, to);
                continue;
            }
            std::vector<Entity> made;
            if (type == "LINE") made.push_back(LineAt(first, end));
            else if (type == "CIRCLE" || type == "ARC") made.push_back(ArcAt(first, end, type == "CIRCLE"));
            else if (type == "LWPOLYLINE") made.push_back(LwPolylineAt(first, end));
            else if (type == "POLYLINE") {
                std::optional<Entity> poly = PolylineAt(first, end, to, i);
                if (poly) made.push_back(std::move(*poly));
            } else if (type == "TEXT" || type == "MTEXT") made.push_back(TextAt(first, end, type == "MTEXT"));
            else if (type == "ELLIPSE") made.push_back(EllipseAt(first, end));
            else if (type == "SPLINE") made.push_back(SplineAt(first, end));
            else if (type == "INSERT") {
                Insert(first, end, place, depth, out);
                if (Flag(first, end, 66)) i = SkipSequence(i, to);
                continue;
            } else {
                ++mySkipped[std::string(type)];
                continue;
            }
            for (Entity& e : made) {
                if (place) Place(e, *place);
                out.push_back(std::move(e));
            }
        }
    }

    bool Paper(std::size_t from, std::size_t to) const {
        for (std::size_t k = from; k < to; ++k)
            if (myPairs[k].code == 67 && Integer(myPairs[k].value) == 1) return true;
        return false;
    }
    bool Flag(std::size_t from, std::size_t to, int code) const {
        for (std::size_t k = from; k < to; ++k)
            if (myPairs[k].code == code && Integer(myPairs[k].value) != 0) return true;
        return false;
    }
    /// Past the VERTEX / ATTRIB entities and their SEQEND.
    std::size_t SkipSequence(std::size_t i, std::size_t to) const {
        while (i < to && !(myPairs[i].code == 0 && Trim(myPairs[i].value) == "SEQEND")) ++i;
        if (i < to) ++i;
        while (i < to && myPairs[i].code != 0) ++i;
        return i;
    }

    void CommonAt(std::size_t from, std::size_t to, Entity& e) const {
        for (std::size_t k = from; k < to; ++k) {
            if (myPairs[k].code == 8) e.layer = UnescapeText(Text(Trim(myPairs[k].value)));
            else if (myPairs[k].code == 62) e.color = Integer(myPairs[k].value);
        }
        if (e.layer.empty()) e.layer = "0";
        if (e.color < 0 || e.color > 256) e.color = 256;   // 0 (BYBLOCK) stays: an insert gives it its colour
    }

    Entity LineAt(std::size_t from, std::size_t to) const {
        Entity e;
        e.kind = Kind::Line;
        CommonAt(from, to, e);
        for (std::size_t k = from; k < to; ++k) {
            const auto& p = myPairs[k];
            if (p.code == 10) e.a.x = Len(p.value);
            else if (p.code == 20) e.a.y = Len(p.value);
            else if (p.code == 11) e.b.x = Len(p.value);
            else if (p.code == 21) e.b.y = Len(p.value);
        }
        return e;
    }

    Entity ArcAt(std::size_t from, std::size_t to, bool circle) const {
        Entity e;
        e.kind = circle ? Kind::Circle : Kind::Arc;
        CommonAt(from, to, e);
        for (std::size_t k = from; k < to; ++k) {
            const auto& p = myPairs[k];
            if (p.code == 10) e.center.x = Len(p.value);
            else if (p.code == 20) e.center.y = Len(p.value);
            else if (p.code == 40) e.radius = Len(p.value);
            else if (p.code == 50) e.startDeg = Number(p.value);
            else if (p.code == 51) e.endDeg = Number(p.value);
        }
        return e;
    }

    Entity LwPolylineAt(std::size_t from, std::size_t to) const {
        Entity e;
        e.kind = Kind::Polyline;
        CommonAt(from, to, e);
        for (std::size_t k = from; k < to; ++k) {
            const auto& p = myPairs[k];
            if (p.code == 70) e.closed = (Integer(p.value) & 1) != 0;
            else if (p.code == 10) e.points.push_back({Len(p.value), 0}), e.bulges.push_back(0.0);
            else if (p.code == 20 && !e.points.empty()) e.points.back().y = Len(p.value);
            else if (p.code == 42 && !e.bulges.empty()) e.bulges.back() = Number(p.value);
        }
        return e;
    }

    std::optional<Entity> PolylineAt(std::size_t from, std::size_t to, std::size_t sectionEnd, std::size_t& next) {
        Entity e;
        e.kind = Kind::Polyline;
        CommonAt(from, to, e);
        int flags = 0;
        for (std::size_t k = from; k < to; ++k)
            if (myPairs[k].code == 70) flags = Integer(myPairs[k].value);
        e.closed = (flags & 1) != 0;
        std::size_t i = to;
        while (i < sectionEnd && myPairs[i].code == 0 && Trim(myPairs[i].value) == "VERTEX") {
            std::size_t end = i + 1;
            while (end < sectionEnd && myPairs[end].code != 0) ++end;
            Vec2 v;
            double bulge = 0.0;
            int vflags = 0;
            for (std::size_t k = i + 1; k < end; ++k) {
                if (myPairs[k].code == 10) v.x = Len(myPairs[k].value);
                else if (myPairs[k].code == 20) v.y = Len(myPairs[k].value);
                else if (myPairs[k].code == 42) bulge = Number(myPairs[k].value);
                else if (myPairs[k].code == 70) vflags = Integer(myPairs[k].value);
            }
            if ((vflags & 16) == 0) {   // not a spline frame point
                e.points.push_back(v);
                e.bulges.push_back(bulge);
            }
            i = end;
        }
        next = SkipSequence(i, sectionEnd);
        if (i < sectionEnd && !(myPairs[i].code == 0 && Trim(myPairs[i].value) == "SEQEND")) next = i;
        if (flags & (16 | 64)) {   // meshes and polyface: not for a blank
            ++mySkipped["POLYLINE (сетка)"];
            return std::nullopt;
        }
        return e;
    }

    Entity TextAt(std::size_t from, std::size_t to, bool mtext) const {
        Entity e;
        e.kind = Kind::Text;
        CommonAt(from, to, e);
        std::string body, tail;
        Vec2 align, direction{1, 0};
        bool aligned = false, hasDirection = false;
        for (std::size_t k = from; k < to; ++k) {
            const auto& p = myPairs[k];
            if (p.code == 10) e.at.x = Len(p.value);
            else if (p.code == 20) e.at.y = Len(p.value);
            else if (p.code == 11) align.x = Len(p.value), hasDirection = true;
            else if (p.code == 21) align.y = Len(p.value);
            else if (p.code == 40) e.height = Len(p.value);
            else if (p.code == 50) e.rotationDeg = Number(p.value);
            else if (p.code == 1) tail = Text(p.value);
            else if (p.code == 3) body += Text(p.value);
            else if ((p.code == 72 || p.code == 73) && Integer(p.value) != 0) aligned = true;
        }
        body += tail;
        if (mtext) {
            body = PlainMText(body);
            if (hasDirection && (align.x != 0 || align.y != 0)) {
                direction = align;
                e.rotationDeg = std::atan2(direction.y, direction.x) * 180.0 / kPi;
            }
        } else if (aligned && hasDirection) {
            e.at = align;   // justified text: its alignment point is where it is
        }
        e.text = UnescapeText(body);
        if (e.height <= 0) e.height = 2.5;
        return e;
    }

    Entity EllipseAt(std::size_t from, std::size_t to) const {
        Vec2 c, major;
        double ratio = 1, t0 = 0, t1 = 2 * kPi;
        std::string layer;
        Entity tmp;
        CommonAt(from, to, tmp);
        for (std::size_t k = from; k < to; ++k) {
            const auto& p = myPairs[k];
            if (p.code == 10) c.x = Len(p.value);
            else if (p.code == 20) c.y = Len(p.value);
            else if (p.code == 11) major.x = Len(p.value);
            else if (p.code == 21) major.y = Len(p.value);
            else if (p.code == 40) ratio = Number(p.value);
            else if (p.code == 41) t0 = Number(p.value);
            else if (p.code == 42) t1 = Number(p.value);
        }
        if (t1 <= t0) t1 += 2 * kPi;
        const Vec2 minor = major.Perp() * ratio;
        std::vector<Vec2> pts;
        const int n = std::max(16, static_cast<int>((t1 - t0) / (2 * kPi) * 96));
        for (int i = 0; i <= n; ++i) {
            const double t = t0 + (t1 - t0) * i / n;
            pts.push_back(c + major * std::cos(t) + minor * std::sin(t));
        }
        const bool full = std::abs(t1 - t0 - 2 * kPi) < 1e-9;
        if (full) pts.pop_back();
        return Polyline(std::move(pts), full, tmp.layer, tmp.color);
    }

    Entity SplineAt(std::size_t from, std::size_t to) const {
        Entity tmp;
        CommonAt(from, to, tmp);
        int flags = 0, degree = 3;
        std::vector<double> knots, weights;
        std::vector<Vec2> ctrl, fit;
        for (std::size_t k = from; k < to; ++k) {
            const auto& p = myPairs[k];
            if (p.code == 70) flags = Integer(p.value);
            else if (p.code == 71) degree = Integer(p.value);
            else if (p.code == 40) knots.push_back(Number(p.value));
            else if (p.code == 41) weights.push_back(Number(p.value));
            else if (p.code == 10) ctrl.push_back({Len(p.value), 0});
            else if (p.code == 20 && !ctrl.empty()) ctrl.back().y = Len(p.value);
            else if (p.code == 11) fit.push_back({Len(p.value), 0});
            else if (p.code == 21 && !fit.empty()) fit.back().y = Len(p.value);
        }
        std::vector<Vec2> pts = !ctrl.empty() ? SplinePoints(degree, knots, ctrl, weights) : ThroughPoints(fit, (flags & 1) != 0);
        return Polyline(std::move(pts), (flags & 1) != 0, tmp.layer, tmp.color);
    }

    void Insert(std::size_t from, std::size_t to, const Placement* outer, int depth, std::vector<Entity>& out) {
        std::string name;
        Placement p;
        Entity self;
        CommonAt(from, to, self);
        for (std::size_t k = from; k < to; ++k) {
            const auto& q = myPairs[k];
            if (q.code == 2) name = Text(Trim(q.value));
            else if (q.code == 10) p.at.x = Len(q.value);
            else if (q.code == 20) p.at.y = Len(q.value);
            else if (q.code == 41) p.sx = Number(q.value);
            else if (q.code == 42) p.sy = Number(q.value);
            else if (q.code == 50) p.rotDeg = Number(q.value);
        }
        const auto block = myBlocks.find(name);
        if (block == myBlocks.end() || depth > 8) {
            ++mySkipped["INSERT"];
            return;
        }
        p.base = block->second.base;
        std::vector<Entity> inside;
        ReadEntities(block->second.from, block->second.to, nullptr, depth + 1, inside);
        for (Entity& e : inside) {
            // As AutoCAD draws them: what is on layer 0 inside a block is on the insert's layer, BYBLOCK colour is the insert's.
            if (e.layer == "0") e.layer = self.layer;
            if (e.color == 0) e.color = self.color;
            Place(e, p);
            if (outer) Place(e, *outer);
            out.push_back(std::move(e));
        }
    }

    struct Block {
        Vec2        base;
        std::size_t from = 0, to = 0;
    };

    ReadReport&                 myReport;
    std::vector<Pair>           myPairs;
    bool                        myBad = false;
    CodePage                    myPage = CodePage::Cp1252;
    double                      myScale = 1.0;
    std::map<std::string, Block> myBlocks;
    std::optional<std::pair<std::size_t, std::size_t>> myEntitiesAt;
    std::map<std::string, int>  mySkipped;
    Drawing                     myDrawing;
};

} // namespace

std::string UnescapeText(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 6 < text.size() + 0 && (text[i + 1] == 'U' || text[i + 1] == 'u') && text[i + 2] == '+') {
            unsigned cp = 0;
            const auto r = std::from_chars(text.data() + i + 3, text.data() + std::min(text.size(), i + 7), cp, 16);
            if (r.ec == std::errc{} && r.ptr == text.data() + i + 7) {
                AppendUtf8(out, cp);
                i += 6;
                continue;
            }
        }
        if (text[i] == '%' && i + 2 < text.size() && text[i + 1] == '%') {
            const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
            if (k == 'd') { out += "\xC2\xB0"; i += 2; continue; }
            if (k == 'c') { out += "\xE2\x8C\x80"; i += 2; continue; }   // ⌀
            if (k == 'p') { out += "\xC2\xB1"; i += 2; continue; }
            if (k == '%') { out += '%'; i += 2; continue; }
            if (k == 'u' || k == 'o') { i += 2; continue; }   // underline / overline switches
        }
        out += text[i];
    }
    return out;
}

Expected<Drawing> Read(std::string_view text, ReadReport* report) {
    if (text.starts_with("AutoCAD Binary DXF")) return Error{"Двоичный DXF не поддерживается: сохраните его в AutoCAD как текстовый (ASCII) DXF"};
    ReadReport local;
    Reader r(text, report ? *report : local);
    return r.Run();
}

} // namespace bz::dxf
