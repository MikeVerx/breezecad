#include "document/StandardPacks.h"

#include "core/Expression.h"
#include "core/Folders.h"
#include "document/BzcFile.h"
#include "document/StandardParts.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace bz::doc::parts {

using Json = nlohmann::json;

namespace {

/// Packs bigger than this are refused: a library table is kilobytes.
constexpr std::uintmax_t kMaxPackBytes = 16u << 20;

bool ValidId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    const char first = id.front();
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || (first >= '0' && first <= '9'))) return false;
    return std::ranges::all_of(id, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

/// Names the document gives its dimensions ("d12") would clash with a column.
bool LooksLikeDimension(std::string_view name) {
    return name.size() > 1 && name[0] == 'd' && std::ranges::all_of(name.substr(1), [](char c) { return c >= '0' && c <= '9'; });
}

/// Reading JSON without exceptions: each Read* returns false on a wrong type.
class Reader {
public:
    explicit Reader(std::string place) : myPlace(std::move(place)) {}

    Error Fail(std::string_view what) const { return Error{std::format("{}: {}", myPlace, what)}; }

    bool String(const Json& j, const char* key, std::string& out, bool required, Error& err) const {
        const auto it = j.find(key);
        if (it == j.end() || it->is_null()) {
            if (required) err = Fail(std::format("нет поля «{}»", key));
            return !required;
        }
        if (!it->is_string()) {
            err = Fail(std::format("«{}» должно быть строкой", key));
            return false;
        }
        out = it->get<std::string>();
        if (required && out.empty()) {
            err = Fail(std::format("«{}» пустое", key));
            return false;
        }
        return true;
    }

    /// An expression: a string, or a number written as one.
    bool Expr(const Json& j, const char* key, std::string& out, bool required, Error& err) const {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number()) {
            out = PackNumber(it->get<double>());
            return true;
        }
        return String(j, key, out, required, err);
    }

private:
    std::string myPlace;
};

std::string ExprOf(const Json& v) { return v.is_number() ? PackNumber(v.get<double>()) : v.is_string() ? v.get<std::string>() : std::string{}; }

Expected<PackShape> ReadShape(const Json& j, const Reader& r) {
    if (!j.is_object() || j.size() != 1) return r.Fail("фигура — объект с одним ключом: rect, circle, polygon, hexagon или axis");
    const auto& [type, body] = *j.items().begin();
    PackShape s;
    Error err;
    const Reader in(std::format("фигура {}", type));
    if (!body.is_object()) return in.Fail("параметры фигуры — объект");
    if (type == "rect") {
        s.type = PackShape::Type::Rect;
        if (!in.Expr(body, "w", s.w, true, err) || !in.Expr(body, "h", s.h, true, err) || !in.Expr(body, "r", s.r, false, err) ||
            !in.Expr(body, "x", s.x, false, err) || !in.Expr(body, "y", s.y, false, err))
            return err;
    } else if (type == "circle") {
        s.type = PackShape::Type::Circle;
        if (!in.Expr(body, "d", s.d, true, err) || !in.Expr(body, "x", s.x, false, err) || !in.Expr(body, "y", s.y, false, err))
            return err;
    } else if (type == "hexagon") {
        s.type = PackShape::Type::Hexagon;
        if (!in.Expr(body, "s", s.s, true, err)) return err;
    } else if (type == "polygon" || type == "axis") {
        const bool polygon = type == "polygon";
        s.type = polygon ? PackShape::Type::Polygon : PackShape::Type::Axis;
        const char* key = polygon ? "points" : "ends";
        const auto pts = body.find(key);
        if (pts == body.end() || !pts->is_array() || (polygon ? pts->size() < 3 : pts->size() != 2))
            return in.Fail(polygon ? "нужно «points»: не меньше трёх точек [x, y]" : "нужно «ends»: две точки [x, y]");
        for (const Json& p : *pts) {
            if (!p.is_array() || p.size() != 2 || ExprOf(p[0]).empty() || ExprOf(p[1]).empty())
                return in.Fail("точка — пара [x, y] (числа или выражения)");
            s.points.push_back({ExprOf(p[0]), ExprOf(p[1])});
        }
        if (const auto a = body.find("axis"); polygon && a != body.end()) {
            if (!a->is_boolean()) return in.Fail("«axis» — true или false");
            s.axis = a->get<bool>();
        }
        if (const auto r = body.find("radii"); polygon && r != body.end()) {
            if (!r->is_array() || r->size() != s.points.size()) return in.Fail("«radii» — радиус на каждую точку (0 — острый угол)");
            for (const Json& v : *r) {
                if (!v.is_number() && !v.is_string()) return in.Fail("радиус — число или выражение");
                s.radii.push_back(ExprOf(v));
            }
        }
    } else {
        return r.Fail(std::format("неизвестная фигура «{}» (rect, circle, polygon, hexagon, axis)", type));
    }
    return s;
}

Expected<PackStep> ReadStep(const Json& j, const Reader& r) {
    if (!j.is_object() || j.size() != 1) return r.Fail("шаг — объект с одним ключом: sketch, extrude или revolve");
    const auto& [type, body] = *j.items().begin();
    PackStep step;
    Error err;
    const Reader in(std::format("шаг {}", type));
    if (!body.is_object()) return in.Fail("параметры шага — объект");
    const auto readOperation = [&]() -> bool {
        std::string op;
        if (!in.String(body, "operation", op, false, err)) return false;
        if (op.empty()) return true;
        if (op == "new") step.operation = PackStep::Operation::New;
        else if (op == "join") step.operation = PackStep::Operation::Join;
        else if (op == "cut") step.operation = PackStep::Operation::Cut;
        else {
            err = in.Fail(std::format("«operation»: new, join или cut, а не «{}»", op));
            return false;
        }
        return true;
    };
    if (type == "sketch") {
        step.type = PackStep::Type::Sketch;
        if (!in.String(body, "plane", step.plane, true, err)) return err;
        if (step.plane != "XY" && step.plane != "XZ" && step.plane != "YZ") return in.Fail("«plane»: XY, XZ или YZ");
        const auto shapes = body.find("shapes");
        if (shapes == body.end() || !shapes->is_array() || shapes->empty()) return in.Fail("нужен непустой список «shapes»");
        for (const Json& s : *shapes) {
            Expected<PackShape> shape = ReadShape(s, in);
            if (!shape) return shape.GetError();
            step.shapes.push_back(std::move(*shape));
        }
    } else if (type == "extrude") {
        step.type = PackStep::Type::Extrude;
        std::string dir;
        if (!in.Expr(body, "distance", step.distance, true, err) || !in.String(body, "direction", dir, false, err) || !readOperation())
            return err;
        if (dir.empty() || dir == "normal") step.direction = PackStep::Direction::Normal;
        else if (dir == "reversed") step.direction = PackStep::Direction::Reversed;
        else if (dir == "symmetric") step.direction = PackStep::Direction::Symmetric;
        else return in.Fail("«direction»: normal, reversed или symmetric");
    } else if (type == "revolve") {
        step.type = PackStep::Type::Revolve;
        if (!readOperation()) return err;
    } else {
        return r.Fail(std::format("неизвестный шаг «{}» (sketch, extrude, revolve)", type));
    }
    return step;
}

Expected<PackKind> ReadKind(const Json& j) {
    PackKind k;
    Error err;
    if (!j.is_object()) return Error{"вид изделия — объект"};
    Reader r("вид изделия");
    if (!r.String(j, "id", k.id, true, err)) return err;
    r = Reader(std::format("вид «{}»", k.id));
    if (!ValidId(k.id)) return r.Fail("id — латиница, цифры, «.», «-», «_», до 64 знаков");
    if (!r.String(j, "group", k.group, true, err) || !r.String(j, "variety", k.variety, true, err) ||
        !r.String(j, "title", k.title, true, err) || !r.String(j, "noun", k.noun, true, err) ||
        !r.String(j, "size_title", k.sizeTitle, false, err) || !r.String(j, "appearance", k.appearance, false, err) ||
        !r.String(j, "material", k.material, false, err))
        return err;
    if (k.appearance.empty()) k.appearance = "std.steel";
    if (k.material.empty()) k.material = "Сталь";
    if (const auto doc = j.find("document"); doc != j.end()) {   // a part the user saved: no recipe
        if (j.contains("build")) return r.Fail("либо «build» (рецепт), либо «document» (деталь)");
        if (!doc->is_object()) return r.Fail("«document» — деталь (объект, как в файле .bzc)");
        k.document = doc->dump();
        if (const Expected<Document> d = DocumentFromJson(k.document); !d)
            return r.Fail(std::format("«document» не читается: {}", d.GetError().message));
        return k;
    }
    const auto build = j.find("build");
    if (build == j.end() || !build->is_array() || build->empty()) return r.Fail("нужен непустой список шагов «build»");
    bool sketch = false, solid = false, axis = false;
    for (const Json& s : *build) {
        Expected<PackStep> step = ReadStep(s, r);
        if (!step) return Error{std::format("вид «{}», {}", k.id, step.GetError().message)};
        if (step->type == PackStep::Type::Sketch) {
            sketch = true;
            axis = std::ranges::any_of(step->shapes, [](const PackShape& sh) { return sh.type == PackShape::Type::Axis || sh.axis; });
        } else if (!sketch) return r.Fail("операция до первого эскиза");
        else if (step->type == PackStep::Type::Revolve && !axis) return r.Fail("вращение: в эскизе перед ним нет осевой (axis)");
        else {
            const bool explicitOp = s.begin()->contains("operation");
            if (!solid && explicitOp && step->operation != PackStep::Operation::New) return r.Fail("первая операция должна создавать тело (new)");
            if (solid && !explicitOp) step->operation = PackStep::Operation::Join;   // later ones join by default
            solid = true;
        }
        k.build.push_back(std::move(*step));
    }
    if (!solid) return r.Fail("рецепт не создаёт тела (нет extrude или revolve)");
    if (const auto e = j.find("envelope"); e != j.end()) {
        if (!e->is_array() || e->size() != 3) return r.Fail("«envelope» — три выражения [x, y, z]");
        for (std::size_t i = 0; i < 3; ++i) k.envelope[i] = ExprOf((*e)[i]);
    }
    return k;
}

Expected<PackStandard> ReadStandard(const Json& j) {
    PackStandard s;
    Error err;
    if (!j.is_object()) return Error{"стандарт — объект"};
    Reader r("стандарт");
    if (!r.String(j, "id", s.id, true, err)) return err;
    r = Reader(std::format("стандарт «{}»", s.id));
    if (!ValidId(s.id)) return r.Fail("id — латиница, цифры, «.», «-», «_», до 64 знаков");
    if (!r.String(j, "title", s.title, true, err) || !r.String(j, "kind", s.kind, true, err) || !r.String(j, "note", s.note, false, err) ||
        !r.String(j, "family", s.family, false, err) || !r.String(j, "size", s.size, false, err) ||
        !r.String(j, "designation", s.designation, false, err))
        return err;
    if (!s.family.empty() && s.family != "ГОСТ" && s.family != "ISO" && s.family != "DIN" && s.family != "*")
        return r.Fail("«family»: ГОСТ, ISO, DIN или * (во всех вкладках)");

    const auto cols = j.find("columns");
    if (cols == j.end() || !cols->is_array() || cols->empty()) return r.Fail("нужен список столбцов «columns»");
    std::set<std::string> names;
    for (const Json& c : *cols) {
        PackColumn col;
        if (c.is_string()) col.name = c.get<std::string>();
        else if (c.is_object()) {
            if (!r.String(c, "name", col.name, true, err) || !r.String(c, "title", col.title, false, err)) return err;
            if (const auto l = c.find("length"); l != c.end()) {
                if (!l->is_boolean()) return r.Fail("«length» столбца — true или false");
                col.length = l->get<bool>();
            }
        } else return r.Fail("столбец — имя или объект {name, title, length}");
        // A size column becomes a variable of the part; a reference one (mass…)
        // only needs a name for templates.
        if (col.length && (!IsValidVariableName(col.name) || col.name == "L" || LooksLikeDimension(col.name)))
            return r.Fail(std::format("имя столбца «{}» не годится для переменной (буква в начале; не единица измерения, "
                                      "не L и не d1, d2…)", col.name));
        if (!col.length && (col.name.empty() || col.name.find_first_of("{} ") != std::string::npos))
            return r.Fail(std::format("имя столбца «{}» — без пробелов и скобок", col.name));
        if (!names.insert(col.name).second) return r.Fail(std::format("столбец «{}» повторяется", col.name));
        s.columns.push_back(std::move(col));
    }

    const auto rows = j.find("rows");
    if (rows == j.end() || !rows->is_array() || rows->empty()) return r.Fail("нужна таблица размеров «rows»");
    for (const Json& row : *rows) {
        if (!row.is_array() || row.size() != s.columns.size())
            return r.Fail(std::format("строка {} — не {} чисел", row.dump(), s.columns.size()));
        std::vector<double> values;
        for (const Json& v : row) {
            if (!v.is_number() || !std::isfinite(v.get<double>())) return r.Fail(std::format("строка {}: не число", row.dump()));
            values.push_back(v.get<double>());
        }
        s.rows.push_back(std::move(values));
    }

    if (const auto labels = j.find("labels"); labels != j.end()) {
        if (!labels->is_array() || labels->size() != s.rows.size()) return r.Fail("«labels» — имя на каждую строку таблицы");
        for (const Json& l : *labels) {
            if (!l.is_string() || l.get<std::string>().empty()) return r.Fail("имя строки в «labels» — непустая строка");
            s.labels.push_back(l.get<std::string>());
        }
    }
    if (s.size.empty() && s.labels.empty()) return r.Fail("нужен «size» (как писать размер) или «labels» (имена строк)");
    if (const auto inputs = j.find("inputs"); inputs != j.end()) {
        if (!inputs->is_array()) return r.Fail("«inputs» — список {name, title, min, max, default}");
        for (const Json& in : *inputs) {
            PackInput input;
            if (!in.is_object() || !r.String(in, "name", input.name, true, err) || !r.String(in, "title", input.title, false, err))
                return in.is_object() ? err : r.Fail("«inputs» — список объектов");
            const auto num = [&](const char* key, double& out) {
                const auto it = in.find(key);
                if (it == in.end() || !it->is_number()) return false;
                out = it->get<double>();
                return std::isfinite(out);
            };
            if (!num("min", input.min) || !num("max", input.max)) return r.Fail(std::format("в «{}» нужны числа min и max", input.name));
            if (!num("default", input.preset)) input.preset = input.min;
            if (input.min <= 0 || input.max < input.min || input.preset < input.min || input.preset > input.max)
                return r.Fail(std::format("«{}»: 0 < min ≤ default ≤ max", input.name));
            if (!IsValidVariableName(input.name) || input.name == "L" || LooksLikeDimension(input.name) || names.contains(input.name))
                return r.Fail(std::format("имя «{}» не годится (как у переменной; не L, не d1…, не как у столбца)", input.name));
            names.insert(input.name);
            if (input.title.empty()) input.title = input.name;
            s.inputs.push_back(std::move(input));
        }
    }
    const auto list = j.find("lengths");
    const auto range = j.find("length");
    if (list != j.end() && range != j.end()) return r.Fail("либо «lengths» (список), либо «length» (диапазон)");
    if (list != j.end()) {
        if (!list->is_array() || list->empty()) return r.Fail("«lengths» — непустой список длин, мм");
        s.length.mode = PackLength::Mode::List;
        for (const Json& v : *list) {
            if (!v.is_number() || v.get<double>() <= 0) return r.Fail("длины в «lengths» — положительные числа");
            s.length.list.push_back(v.get<double>());
        }
    } else if (range != j.end()) {
        if (!range->is_object()) return r.Fail("«length» — {min, max, default}");
        const auto num = [&](const char* key, double& out) {
            const auto it = range->find(key);
            if (it == range->end() || !it->is_number()) return false;
            out = it->get<double>();
            return std::isfinite(out);
        };
        s.length.mode = PackLength::Mode::Free;
        if (!num("min", s.length.min) || !num("max", s.length.max)) return r.Fail("в «length» нужны числа min и max");
        if (!num("default", s.length.preset)) s.length.preset = s.length.min;
        if (s.length.min <= 0 || s.length.max < s.length.min || s.length.preset < s.length.min || s.length.preset > s.length.max)
            return r.Fail("«length»: 0 < min ≤ default ≤ max");
    }
    return s;
}

/// Placeholders "{name}" of a template.
std::vector<std::string> Placeholders(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t i = text.find('{'); i != std::string_view::npos; i = text.find('{', i + 1)) {
        const std::size_t end = text.find('}', i);
        if (end == std::string_view::npos) break;
        out.emplace_back(text.substr(i + 1, end - i - 1));
    }
    return out;
}

/// The value of a column (or L) in an expression.
NameLookup RowLookup(const PackStandard& s, std::size_t row, double length) {
    return [&s, row, length](std::string_view name) -> std::optional<NamedValue> {
        if (name == "L" && s.length.mode != PackLength::Mode::None) return NamedValue{length, Quantity::Length};
        for (std::size_t i = 0; i < s.columns.size(); ++i)
            if (s.columns[i].length && s.columns[i].name == name) return NamedValue{s.rows[row][i], Quantity::Length};
        for (const PackInput& in : s.inputs)
            if (in.name == name) return NamedValue{in.preset, Quantity::Length};
        return std::nullopt;
    };
}

/// Every expression of the recipe, with what it must be (a size > 0, or any coordinate).
template <class F>
void ForEachExpression(const PackKind& k, F&& f) {
    for (const PackStep& step : k.build) {
        if (step.type == PackStep::Type::Extrude) f(step.distance, true);
        for (const PackShape& sh : step.shapes) {
            for (const std::string* e : {&sh.w, &sh.h, &sh.d, &sh.s})
                if (!e->empty()) f(*e, true);
            for (const std::string* e : {&sh.r, &sh.x, &sh.y})
                if (!e->empty()) f(*e, false);
            for (const auto& p : sh.points) f(p[0], false), f(p[1], false);
            for (const std::string& r : sh.radii)
                if (!r.empty()) f(r, false);
        }
    }
    for (const std::string& e : k.envelope)
        if (!e.empty()) f(e, true);
}

} // namespace

std::string PackNumber(double v) {
    std::string s = std::format("{:.3f}", v);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::string FillPackTemplate(std::string_view text, const PackStandard& s, std::size_t row,
                             std::span<const std::pair<std::string, std::string>> extra) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find('{', i);
        const std::size_t close = open == std::string_view::npos ? open : text.find('}', open);
        if (close == std::string_view::npos) {
            out += text.substr(i);
            break;
        }
        out += text.substr(i, open - i);
        const std::string_view name = text.substr(open + 1, close - open - 1);
        bool done = false;
        for (const auto& [key, value] : extra)
            if (key == name) out += value, done = true;
        for (std::size_t c = 0; !done && c < s.columns.size(); ++c)
            if (s.columns[c].name == name) out += PackNumber(s.rows[row][c]), done = true;
        if (!done) out += text.substr(open, close - open + 1);
        i = close + 1;
    }
    return out;
}

namespace {

/// An expression as the file writes it: a number when it is one.
Json ExprJson(const std::string& e) {
    // Plain decimal numbers only ("12", "-2.5"); anything else stays text.
    const bool number = !e.empty() && e.find_first_not_of("0123456789.-") == std::string::npos &&
                        e.find('-', 1) == std::string::npos && std::ranges::count(e, '.') <= 1 && e != "-" && e != ".";
    return number ? Json(std::stod(e)) : Json(e);
}

Json ShapeJson(const PackShape& sh) {
    Json body = Json::object();
    const auto put = [&](const char* key, const std::string& e) {
        if (!e.empty()) body[key] = ExprJson(e);
    };
    const auto points = [&](const char* key) {
        Json pts = Json::array();
        for (const auto& p : sh.points) pts.push_back({ExprJson(p[0]), ExprJson(p[1])});
        body[key] = std::move(pts);
    };
    switch (sh.type) {
        case PackShape::Type::Rect:
            put("w", sh.w), put("h", sh.h), put("r", sh.r), put("x", sh.x), put("y", sh.y);
            return {{"rect", body}};
        case PackShape::Type::Circle: put("d", sh.d), put("x", sh.x), put("y", sh.y); return {{"circle", body}};
        case PackShape::Type::Hexagon: put("s", sh.s); return {{"hexagon", body}};
        case PackShape::Type::Axis: points("ends"); return {{"axis", body}};
        case PackShape::Type::Polygon:
            points("points");
            if (sh.axis) body["axis"] = true;
            if (!sh.radii.empty()) {
                Json radii = Json::array();
                for (const std::string& r : sh.radii) radii.push_back(ExprJson(r.empty() ? "0" : r));
                body["radii"] = std::move(radii);
            }
            return {{"polygon", body}};
    }
    return body;
}

Json StepJson(const PackStep& st) {
    static constexpr const char* kOps[] = {"new", "join", "cut"};
    static constexpr const char* kDirs[] = {"normal", "reversed", "symmetric"};
    switch (st.type) {
        case PackStep::Type::Sketch: {
            Json shapes = Json::array();
            for (const PackShape& sh : st.shapes) shapes.push_back(ShapeJson(sh));
            return {{"sketch", {{"plane", st.plane}, {"shapes", std::move(shapes)}}}};
        }
        case PackStep::Type::Extrude:
            return {{"extrude",
                     {{"distance", ExprJson(st.distance)}, {"direction", kDirs[static_cast<int>(st.direction)]},
                      {"operation", kOps[static_cast<int>(st.operation)]}}}};
        case PackStep::Type::Revolve: return {{"revolve", {{"operation", kOps[static_cast<int>(st.operation)]}}}};
    }
    return {};
}

} // namespace

std::string PackToJson(const Pack& pack) {
    Json root = {{"format", kPackFormat}, {"version", kPackVersion}, {"id", pack.id}, {"title", pack.title}};
    if (!pack.source.empty()) root["source"] = pack.source;
    if (!pack.author.empty()) root["author"] = pack.author;
    if (!pack.version.empty()) root["version_of_data"] = pack.version;
    Json kinds = Json::array();
    for (const PackKind& k : pack.kinds) {
        Json j = {{"id", k.id}, {"group", k.group}, {"variety", k.variety}, {"title", k.title}, {"noun", k.noun},
                  {"appearance", k.appearance}, {"material", k.material}};
        if (!k.sizeTitle.empty()) j["size_title"] = k.sizeTitle;
        if (!k.document.empty()) j["document"] = Json::parse(k.document, nullptr, false);
        else {
            Json build = Json::array();
            for (const PackStep& st : k.build) build.push_back(StepJson(st));
            j["build"] = std::move(build);
            if (std::ranges::all_of(k.envelope, [](const std::string& e) { return !e.empty(); }))
                j["envelope"] = {ExprJson(k.envelope[0]), ExprJson(k.envelope[1]), ExprJson(k.envelope[2])};
        }
        kinds.push_back(std::move(j));
    }
    if (!kinds.empty()) root["kinds"] = std::move(kinds);
    Json standards = Json::array();
    for (const PackStandard& st : pack.standards) {
        Json j = {{"id", st.id}, {"title", st.title}, {"kind", st.kind}, {"size", st.size}};
        if (!st.note.empty()) j["note"] = st.note;
        if (!st.family.empty()) j["family"] = st.family;
        if (!st.designation.empty()) j["designation"] = st.designation;
        Json cols = Json::array();
        for (const PackColumn& c : st.columns) {
            Json col = {{"name", c.name}};
            if (!c.title.empty()) col["title"] = c.title;
            if (!c.length) col["length"] = false;
            cols.push_back(std::move(col));
        }
        j["columns"] = std::move(cols);
        j["rows"] = st.rows;
        if (!st.labels.empty()) j["labels"] = st.labels;
        if (st.length.mode == PackLength::Mode::List) j["lengths"] = st.length.list;
        else if (st.length.mode == PackLength::Mode::Free)
            j["length"] = {{"min", st.length.min}, {"max", st.length.max}, {"default", st.length.preset}};
        if (!st.inputs.empty()) {
            Json inputs = Json::array();
            for (const PackInput& in : st.inputs)
                inputs.push_back({{"name", in.name}, {"title", in.title}, {"min", in.min}, {"max", in.max}, {"default", in.preset}});
            j["inputs"] = std::move(inputs);
        }
        standards.push_back(std::move(j));
    }
    root["standards"] = std::move(standards);
    return root.dump(2);
}

std::string PackSize(const PackStandard& s, std::size_t row) {
    if (row < s.labels.size()) return s.labels[row];
    return FillPackTemplate(s.size, s, row, {});
}

Status CheckStandard(const PackStandard& s, const PackKind& k) {
    const auto fail = [&](std::string_view what) { return Error{std::format("стандарт «{}»: {}", s.id, what)}; };
    std::set<std::string> known;
    for (const PackColumn& c : s.columns)
        if (c.length) known.insert(c.name);
    if (s.length.mode != PackLength::Mode::None) known.insert("L");
    for (const PackInput& in : s.inputs) known.insert(in.name);
    // Placeholders of the size and the designation.
    for (const std::string& p : Placeholders(s.size))
        if (std::ranges::none_of(s.columns, [&](const PackColumn& c) { return c.name == p; }))
            return fail(std::format("в «size» нет столбца «{}»", p));
    for (const std::string& p : Placeholders(s.designation))
        if (p != "size" && p != "L" && p != "title" && p != "noun" && std::ranges::none_of(s.columns, [&](const PackColumn& c) { return c.name == p; }) &&
            std::ranges::none_of(s.inputs, [&](const PackInput& in) { return in.name == p; }))
            return fail(std::format("в «designation» неизвестное поле «{}»", p));
    std::set<std::string> sizes;
    for (std::size_t row = 0; row < s.rows.size(); ++row)
        if (!sizes.insert(PackSize(s, row)).second) return fail(std::format("размер «{}» повторяется", PackSize(s, row)));
    if (!k.document.empty()) {   // a saved part: its variables take the table's values
        const Expected<Document> doc = DocumentFromJson(k.document);
        if (!doc) return fail(doc.GetError().message);
        for (const std::string& name : known)
            if (std::ranges::none_of(doc->variables, [&](const Parameter& v) { return v.name == name; }))
                return fail(std::format("в детали вида «{}» нет переменной «{}»", k.id, name));
        return Status::Ok();
    }
    // Expressions name known variables and give sizes > 0 for every row.
    std::string problem;
    ForEachExpression(k, [&](const std::string& e, bool) {
        if (!problem.empty()) return;
        for (const std::string& name : ReferencedNames(e))
            if (!known.contains(name)) problem = std::format("выражение «{}» вида «{}»: нет переменной «{}»", e, k.id, name);
    });
    if (!problem.empty()) return fail(problem);
    const double length = s.length.mode == PackLength::Mode::List ? s.length.list.front() : s.length.preset;
    for (std::size_t row = 0; row < s.rows.size() && problem.empty(); ++row) {
        const NameLookup lookup = RowLookup(s, row, length);
        ForEachExpression(k, [&](const std::string& e, bool positive) {
            if (!problem.empty()) return;
            const Expected<double> v = EvaluateExpression(e, Quantity::Length, UnitSystem{}, lookup);
            if (!v) problem = std::format("размер «{}», «{}»: {}", PackSize(s, row), e, v.GetError().message);
            else if (positive && !(*v > 0)) problem = std::format("размер «{}»: «{}» = {} — должно быть больше нуля", PackSize(s, row), e, *v);
        });
    }
    if (!problem.empty()) return fail(problem);
    return Status::Ok();
}

Expected<Pack> ParsePack(std::string_view text) {
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return Error{"Пакет — не JSON-объект (файл повреждён?)"};
    Pack pack;
    Error err;
    const Reader r("пакет");
    std::string format;
    if (!r.String(root, "format", format, true, err)) return err;
    if (format != kPackFormat) return r.Fail(std::format("«format» должен быть «{}»", kPackFormat));
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer()) return r.Fail("нет «version» (целое число)");
    if (version->get<int>() > kPackVersion) return r.Fail("пакет новее этой версии BreezeCAD — обновите программу");
    if (version->get<int>() < 1) return r.Fail("«version» — с 1");
    if (!r.String(root, "id", pack.id, true, err) || !r.String(root, "title", pack.title, true, err) ||
        !r.String(root, "source", pack.source, false, err) || !r.String(root, "author", pack.author, false, err) ||
        !r.String(root, "version_of_data", pack.version, false, err))
        return err;
    if (!ValidId(pack.id)) return r.Fail("id — латиница, цифры, «.», «-», «_», до 64 знаков");

    if (const auto kinds = root.find("kinds"); kinds != root.end()) {
        if (!kinds->is_array()) return r.Fail("«kinds» — список");
        for (const Json& k : *kinds) {
            Expected<PackKind> kind = ReadKind(k);
            if (!kind) return kind.GetError();
            if (std::ranges::any_of(pack.kinds, [&](const PackKind& o) { return o.id == kind->id; }))
                return r.Fail(std::format("вид «{}» повторяется", kind->id));
            pack.kinds.push_back(std::move(*kind));
        }
    }
    const auto standards = root.find("standards");
    if (standards == root.end() || !standards->is_array() || standards->empty()) return r.Fail("нужен непустой список «standards»");
    for (const Json& s : *standards) {
        Expected<PackStandard> standard = ReadStandard(s);
        if (!standard) return standard.GetError();
        if (std::ranges::any_of(pack.standards, [&](const PackStandard& o) { return o.id == standard->id; }))
            return r.Fail(std::format("стандарт «{}» повторяется", standard->id));
        // Kinds of this pack are checked now; kinds of other packs when it is added.
        const auto kind = std::ranges::find(pack.kinds, standard->kind, &PackKind::id);
        if (kind != pack.kinds.end())
            if (const Status ok = CheckStandard(*standard, *kind); !ok) return ok.GetError();
        pack.standards.push_back(std::move(*standard));
    }
    return pack;
}

Expected<Pack> ReadPack(const std::filesystem::path& file) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) return Error{"Не удалось открыть файл пакета"};
    if (size > kMaxPackBytes) return Error{"Файл пакета слишком большой (больше 16 МБ)"};
    std::ifstream in(file, std::ios::binary);
    if (!in) return Error{"Не удалось открыть файл пакета"};
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Expected<Pack> pack = ParsePack(text);
    if (pack) pack->file = file;
    return pack;
}

// --- Loaded packs ------------------------------------------------------------

namespace {

struct Registry {
    std::vector<std::unique_ptr<Pack>> packs;
    unsigned                           generation = 1;
};

Registry& Loaded() {
    static Registry registry;
    return registry;
}

} // namespace

const PackKind* FindPackKind(std::string_view id) {
    for (const auto& p : Loaded().packs)
        for (const PackKind& k : p->kinds)
            if (k.id == id) return &k;
    return nullptr;
}

Status AddPack(Pack pack) {
    Registry& reg = Loaded();
    const auto fail = [&](std::string_view what) { return Error{std::format("пакет «{}»: {}", pack.id, what)}; };
    for (const auto& p : reg.packs)
        if (p->id == pack.id) return fail("уже загружен пакет с таким id");
    for (const PackKind& k : pack.kinds)
        if (FindPackKind(k.id)) return fail(std::format("вид «{}» уже есть в другом пакете", k.id));
    for (const PackStandard& s : pack.standards) {
        if (FindStandard(s.id)) return fail(std::format("стандарт «{}» уже есть в библиотеке", s.id));
        const auto own = std::ranges::find(pack.kinds, s.kind, &PackKind::id);
        if (own != pack.kinds.end()) continue;   // checked by ParsePack
        const PackKind* other = FindPackKind(s.kind);
        if (!other) return fail(std::format("стандарт «{}»: нет вида «{}»", s.id, s.kind));
        if (const Status ok = CheckStandard(s, *other); !ok) return fail(ok.GetError().message);
    }
    reg.packs.push_back(std::make_unique<Pack>(std::move(pack)));
    ++reg.generation;
    return Status::Ok();
}

void ClearPacks() {
    Registry& reg = Loaded();
    reg.packs.clear();
    ++reg.generation;
}

std::span<const std::unique_ptr<Pack>> LoadedPacks() { return Loaded().packs; }

unsigned PacksGeneration() { return Loaded().generation; }

PacksLoaded LoadPacks(std::span<const std::filesystem::path> folders) {
    ClearPacks();
    PacksLoaded out;
    for (const std::filesystem::path& folder : folders) {
        std::vector<std::filesystem::path> files;
        for (const auto& e : FolderEntries(folder))
            if (IsFile(e) && e.path().extension() == kPackExtension) files.push_back(e.path());
        std::ranges::sort(files);   // the same order on every system
        for (const std::filesystem::path& f : files) {
            const std::u8string name = f.filename().u8string();
            const std::string shown(name.begin(), name.end());
            Expected<Pack> pack = ReadPack(f);
            if (!pack) {
                out.problems.push_back(std::format("{}: {}", shown, pack.GetError().message));
                continue;
            }
            // The same pack in a later folder (the shipped copy of one the user
            // installed, maybe older): the first one stays, without a word.
            if (std::ranges::any_of(Loaded().packs, [&](const auto& p) { return p->id == pack->id; })) continue;
            if (const Status ok = AddPack(std::move(*pack)); !ok) out.problems.push_back(std::format("{}: {}", shown, ok.GetError().message));
            else ++out.packs;
        }
    }
    return out;
}

Expected<std::filesystem::path> InstallPack(const std::filesystem::path& file, const std::filesystem::path& folder) {
    Expected<Pack> pack = ReadPack(file);
    if (!pack) return pack.GetError();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path target = folder / (pack->id + std::string(kPackExtension));
    if (std::filesystem::exists(target, ec) && std::filesystem::equivalent(file, target, ec)) return target;   // already there
    std::filesystem::copy_file(file, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return Error{"Не удалось скопировать пакет в папку библиотеки"};
    return target;
}

// --- The user's own library ----------------------------------------------------

namespace {

/// Writes `text` to `file` through a temporary copy: a crash midway leaves the old file.
Status WriteAtomically(const std::filesystem::path& file, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) return Error{"Не удалось записать файл библиотеки"};
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) return Error{"Не удалось записать файл библиотеки"};
    return Status::Ok();
}

Expected<Pack> UserPack(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        Pack pack;
        pack.id = std::string(kUserPackId);
        pack.title = "Мои изделия";
        pack.source = "детали пользователя";
        return pack;
    }
    Expected<Pack> pack = ReadPack(file);
    if (!pack) return Error{std::format("Моя библиотека повреждена: {}", pack.GetError().message)};
    if (pack->id != kUserPackId) return Error{"Файл моей библиотеки — чужой пакет"};
    return pack;
}

} // namespace

bool IsUserStandard(std::string_view id) { return id.starts_with("user."); }

Expected<std::string> AddUserItem(const std::filesystem::path& file, const Document& part, const UserItem& item) {
    if (item.title.empty()) return Error{"Нужно название изделия"};
    if (item.variables.empty()) return Error{"Выберите переменные детали — они станут столбцами таблицы размеров"};
    Expected<Pack> loaded = UserPack(file);
    if (!loaded) return loaded.GetError();
    Pack pack = std::move(*loaded);

    // The next free number for the ids.
    int n = 1;
    const auto taken = [&](int k) {
        const std::string tail = "." + std::to_string(k);
        return std::ranges::any_of(pack.kinds, [&](const PackKind& x) { return x.id.ends_with(tail); }) ||
               std::ranges::any_of(pack.standards, [&](const PackStandard& x) { return x.id.ends_with(tail); });
    };
    while (taken(n)) ++n;

    PackKind kind;
    kind.id = std::format("user.kind.{}", n);
    kind.group = item.group.empty() ? std::string("Мои изделия") : item.group;
    kind.variety = item.title;
    kind.title = item.title;
    kind.noun = item.title;
    kind.material = part.info.material.empty() ? std::string("Сталь") : part.info.material;
    kind.document = DocumentToJson(part);

    PackStandard st;
    st.id = std::format("user.{}", n);
    st.title = item.standard.empty() ? std::string("Свои") : item.standard;
    st.kind = kind.id;
    st.family = "*";
    std::vector<std::size_t> columns;   // positions in item.variables that are columns (not L)
    std::string size;
    for (std::size_t i = 0; i < item.variables.size(); ++i) {
        const std::string& name = item.variables[i];
        const auto v = std::ranges::find(part.variables, name, &Parameter::name);
        if (v == part.variables.end()) return Error{std::format("В детали нет переменной «{}»", name)};
        if (name == "L") {   // the length: any, the part's value first
            st.length.mode = PackLength::Mode::Free;
            st.length.min = std::min(1.0, v->value);
            st.length.max = std::max(100000.0, v->value);
            st.length.preset = v->value;
            continue;
        }
        columns.push_back(i);
        st.columns.push_back({name, name, true});
        size += (size.empty() ? "{" : "\xC3\x97{") + name + "}";
    }
    if (st.columns.empty()) return Error{"Кроме L нужна хотя бы одна переменная — размер в таблице"};
    st.size = size;
    std::string names;
    for (const PackColumn& c : st.columns) names += (names.empty() ? "" : "\xC3\x97") + c.name;
    kind.sizeTitle = std::format("Размер {}, мм", names);
    st.designation = st.length.mode == PackLength::Mode::Free ? "{noun} {size}, L={L}" : "{noun} {size}";
    std::vector<std::vector<double>> rows = item.rows;
    if (rows.empty()) {   // the part as it is: one size
        std::vector<double> row;
        for (const std::string& name : item.variables) row.push_back(std::ranges::find(part.variables, name, &Parameter::name)->value);
        rows.push_back(std::move(row));
    }
    for (const std::vector<double>& row : rows) {
        if (row.size() != item.variables.size()) return Error{"В строке таблицы не столько чисел, сколько выбрано переменных"};
        std::vector<double> values;
        for (const std::size_t i : columns) {
            if (!std::isfinite(row[i])) return Error{"В таблице не число"};
            values.push_back(row[i]);
        }
        st.rows.push_back(std::move(values));
    }
    pack.kinds.push_back(std::move(kind));
    pack.standards.push_back(st);

    // Written as the file is read back: the same checks as any pack.
    const std::string text = PackToJson(pack);
    if (const Expected<Pack> check = ParsePack(text); !check) return check.GetError();
    if (const Status ok = WriteAtomically(file, text); !ok) return ok.GetError();
    return st.id;
}

Status RemoveUserItem(const std::filesystem::path& file, std::string_view standardId) {
    Expected<Pack> loaded = UserPack(file);
    if (!loaded) return loaded.GetError();
    Pack pack = std::move(*loaded);
    const auto it = std::ranges::find(pack.standards, standardId, &PackStandard::id);
    if (it == pack.standards.end()) return Error{"Такого изделия нет в моей библиотеке"};
    const std::string kind = it->kind;
    pack.standards.erase(it);
    if (std::ranges::none_of(pack.standards, [&](const PackStandard& s) { return s.kind == kind; }))
        std::erase_if(pack.kinds, [&](const PackKind& k) { return k.id == kind; });
    if (pack.standards.empty()) {   // nothing left: no pack (a pack needs a standard)
        std::error_code ec;
        std::filesystem::remove(file, ec);
        return ec ? Status(Error{"Не удалось удалить файл моей библиотеки"}) : Status::Ok();
    }
    return WriteAtomically(file, PackToJson(pack));
}

} // namespace bz::doc::parts
