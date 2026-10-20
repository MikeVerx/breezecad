#include "core/Expression.h"

#include "core/Math.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <vector>

namespace bz {
namespace {

/// Exponents of base dimensions. A plain number has both at zero.
struct Dim {
    int length = 0;
    int angle  = 0;

    bool IsNone() const { return length == 0 && angle == 0; }
    bool operator==(const Dim&) const = default;
};

struct Value {
    double      v = 0.0;
    Dim         dim;
    std::size_t from = 0, to = 0;   ///< the text it was read from
};

/// A plain part of an expression that took the field's unit (see Adopt).
struct Adoption {
    std::size_t from = 0, to = 0;
    Dim         dim;
};

Dim DimOf(Quantity q) {
    switch (q) {
        case Quantity::Length: return {1, 0};
        case Quantity::Angle:  return {0, 1};
        case Quantity::Scalar: return {0, 0};
    }
    return {};
}

bool IsIdentStart(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalpha(u) || c == '_' || u >= 0x80;   // UTF-8 letters allowed
}

bool IsIdentChar(char c) {
    return IsIdentStart(c) || std::isdigit(static_cast<unsigned char>(c));
}

struct UnitSuffix {
    std::string_view name;
    double           scale;   // to mm or rad
    Dim              dim;
};

constexpr std::array<UnitSuffix, 7> kUnits{{
    {"mm", 1.0, {1, 0}},
    {"cm", 10.0, {1, 0}},
    {"m", 1000.0, {1, 0}},
    {"in", 25.4, {1, 0}},
    {"deg", kPi / 180.0, {0, 1}},
    {"\xC2\xB0", kPi / 180.0, {0, 1}},   // degree sign
    {"rad", 1.0, {0, 1}},
}};

constexpr std::array<std::string_view, 11> kFunctions{
    "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "abs", "min", "max", "round"};

const UnitSuffix* FindUnit(std::string_view word) {
    for (const auto& u : kUnits)
        if (u.name == word) return &u;
    return nullptr;
}

bool IsFunction(std::string_view word) {
    for (auto f : kFunctions)
        if (f == word) return true;
    return false;
}

/// Recursive-descent parser. Grammar:
///   expr   := term (('+'|'-') term)*
///   term   := power (('*'|'/') power)*
///   power  := unary ('^' power)?
///   unary  := ('-'|'+') unary | primary
///   primary:= number unit? | name | func '(' args ')' | '(' expr ')'
class Parser {
public:
    Parser(std::string_view text, const UnitSystem& units, const NameLookup& lookup)
        : myText(text), myUnits(units), myLookup(lookup) {}

    Expected<Value> ParseAll() {
        Expected<Value> v = ParseExpr();
        if (!v) return v;
        SkipSpaces();
        if (myPos != myText.size())
            return Error{std::format("Лишние символы: «{}»", myText.substr(myPos))};
        return v;
    }

    /// Converts a plain number to `dim` by applying the document units.
    Value Adopt(Value plain, Dim dim) const {
        if (dim == Dim{1, 0} || dim == Dim{0, 1}) myAdopted.push_back({plain.from, plain.to, dim});
        if (dim == Dim{1, 0}) plain.v = myUnits.ToInternal(plain.v, Quantity::Length);
        if (dim == Dim{0, 1}) plain.v = myUnits.ToInternal(plain.v, Quantity::Angle);
        plain.dim = dim;
        return plain;
    }

private:
    /// The parser is recursive: nested brackets, unary signs and powers each
    /// take stack, and a pasted "((((…" of thousands overflowed it (audit
    /// B-10). A real expression never nests this deep.
    static constexpr int kMaxDepth = 200;

    struct DepthGuard {
        int& depth;
        explicit DepthGuard(int& d) : depth(++d) {}
        ~DepthGuard() { --depth; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
        bool TooDeep() const { return depth > kMaxDepth; }
    };

    Expected<Value> ParseExpr() {
        Expected<Value> lhs = ParseTerm();
        if (!lhs) return lhs;
        for (;;) {
            SkipSpaces();
            const char op = Peek();
            if (op != '+' && op != '-') return lhs;
            ++myPos;
            Expected<Value> rhs = ParseTerm();
            if (!rhs) return rhs;
            Expected<Value> sum = AddSub(*lhs, *rhs, op == '-');
            if (!sum) return sum;
            sum->from = lhs->from;
            sum->to = rhs->to;
            lhs = sum;
        }
    }

    Expected<Value> AddSub(Value a, Value b, bool subtract) const {
        if (a.dim.IsNone() && !b.dim.IsNone()) a = Adopt(a, b.dim);
        if (b.dim.IsNone() && !a.dim.IsNone()) b = Adopt(b, a.dim);
        if (!(a.dim == b.dim)) return Error{"Нельзя складывать величины разных единиц"};
        return Value{subtract ? a.v - b.v : a.v + b.v, a.dim};
    }

    Expected<Value> ParseTerm() {
        Expected<Value> lhs = ParsePower();
        if (!lhs) return lhs;
        for (;;) {
            SkipSpaces();
            const char op = Peek();
            if (op != '*' && op != '/') return lhs;
            ++myPos;
            Expected<Value> rhs = ParsePower();
            if (!rhs) return rhs;
            const std::size_t from = lhs->from;
            if (op == '*') {
                lhs = Value{lhs->v * rhs->v,
                            {lhs->dim.length + rhs->dim.length, lhs->dim.angle + rhs->dim.angle}, from, rhs->to};
            } else {
                if (rhs->v == 0.0) return Error{"Деление на ноль"};
                lhs = Value{lhs->v / rhs->v,
                            {lhs->dim.length - rhs->dim.length, lhs->dim.angle - rhs->dim.angle}, from, rhs->to};
            }
        }
    }

    Expected<Value> ParsePower() {
        const DepthGuard guard(myDepth);
        if (guard.TooDeep()) return Error{"Слишком сложное выражение"};
        Expected<Value> base = ParseUnary();
        if (!base) return base;
        SkipSpaces();
        if (Peek() != '^') return base;
        ++myPos;
        Expected<Value> exponent = ParsePower();
        if (!exponent) return exponent;
        if (!exponent->dim.IsNone()) return Error{"Показатель степени должен быть числом"};
        const double e = exponent->v;
        Dim dim = base->dim;
        if (!dim.IsNone()) {
            if (std::round(e) != e) return Error{"Дробная степень величины с единицами"};
            const int n = static_cast<int>(e);
            dim = {dim.length * n, dim.angle * n};
        }
        return Value{std::pow(base->v, e), dim, base->from, exponent->to};
    }

    Expected<Value> ParseUnary() {
        const DepthGuard guard(myDepth);
        if (guard.TooDeep()) return Error{"Слишком сложное выражение"};
        SkipSpaces();
        if (Peek() == '-' || Peek() == '+') {
            const std::size_t from = myPos;
            const bool negate = Peek() == '-';
            ++myPos;
            Expected<Value> v = ParseUnary();
            if (v && negate) v->v = -v->v;
            if (v) v->from = from;
            return v;
        }
        return ParsePrimary();
    }

    Expected<Value> ParsePrimary() {
        SkipSpaces();
        const char c = Peek();
        const std::size_t from = myPos;
        if (c == '(') {
            ++myPos;
            Expected<Value> v = ParseExpr();
            if (!v) return v;
            SkipSpaces();
            if (Peek() != ')') return Error{"Не хватает закрывающей скобки"};
            ++myPos;
            v->from = from;
            v->to = myPos;
            return v;
        }
        const auto spanned = [&](Expected<Value> v) {
            if (v) {
                v->from = from;
                v->to = myPos;
            }
            return v;
        };
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return spanned(ParseNumber());
        if (IsIdentStart(c)) return spanned(ParseName());
        if (c == '\0') return Error{"Пустое выражение"};
        return Error{std::format("Неожиданный символ «{}»", c)};
    }

    Expected<Value> ParseNumber() {
        std::string digits;
        while (myPos < myText.size()) {
            const char c = myText[myPos];
            const bool decimalComma = c == ',' && myCallDepth == 0 && myPos + 1 < myText.size() &&
                                      std::isdigit(static_cast<unsigned char>(myText[myPos + 1]));
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                digits += c;
            } else if (decimalComma) {
                digits += '.';
            } else if ((c == 'e' || c == 'E') && myPos + 1 < myText.size() &&
                       (std::isdigit(static_cast<unsigned char>(myText[myPos + 1])) ||
                        myText[myPos + 1] == '-' || myText[myPos + 1] == '+')) {
                digits += c;
                digits += myText[++myPos];
            } else {
                break;
            }
            ++myPos;
        }
        char* end = nullptr;
        const double v = std::strtod(digits.c_str(), &end);
        if (end == digits.c_str() || *end != '\0') return Error{std::format("Неверное число «{}»", digits)};

        // Optional unit directly after the number (spaces allowed).
        const std::size_t save = myPos;
        SkipSpaces();
        const std::string word = ReadWord();
        if (const UnitSuffix* unit = FindUnit(word)) return Value{v * unit->scale, unit->dim};
        myPos = save;
        return Value{v, {}};
    }

    Expected<Value> ParseName() {
        const std::string name = ReadWord();
        SkipSpaces();
        if (Peek() == '(') {
            if (!IsFunction(name)) return Error{std::format("Неизвестная функция «{}»", name)};
            return ParseCall(name);
        }
        if (name == "pi") return Value{kPi, {}};
        if (FindUnit(name)) return Error{std::format("Единица «{}» без числа", name)};
        if (!myLookup) return Error{std::format("Неизвестное имя «{}»", name)};
        const std::optional<NamedValue> named = myLookup(name);
        if (!named) return Error{std::format("Неизвестное имя «{}»", name)};
        return Value{named->value, DimOf(named->quantity)};
    }

    Expected<Value> ParseCall(const std::string& name) {
        ++myPos;   // '('
        ++myCallDepth;
        std::vector<Value> args;
        SkipSpaces();
        while (Peek() != ')') {
            Expected<Value> arg = ParseExpr();
            if (!arg) return arg;
            args.push_back(*arg);
            SkipSpaces();
            if (Peek() == ';' || Peek() == ',') {
                ++myPos;
                continue;
            }
            if (Peek() != ')') return Error{std::format("Ожидалась «)» после аргументов {}", name)};
        }
        ++myPos;
        --myCallDepth;
        return ApplyFunction(name, args);
    }

    Expected<Value> ApplyFunction(const std::string& name, std::vector<Value>& args) const {
        const auto expectArgs = [&](std::size_t n) -> Status {
            if (args.size() != n) return Error{std::format("Функция {} ждёт аргументов: {}", name, n)};
            return Status::Ok();
        };
        if (name == "sin" || name == "cos" || name == "tan") {
            if (Status s = expectArgs(1); !s) return s.GetError();
            Value a = args[0].dim.IsNone() ? Adopt(args[0], {0, 1}) : args[0];
            if (!(a.dim == Dim{0, 1})) return Error{std::format("Аргумент {} должен быть углом", name)};
            const double r = name == "sin" ? std::sin(a.v) : name == "cos" ? std::cos(a.v) : std::tan(a.v);
            return Value{r, {}};
        }
        if (name == "asin" || name == "acos" || name == "atan") {
            if (Status s = expectArgs(1); !s) return s.GetError();
            if (!args[0].dim.IsNone()) return Error{std::format("Аргумент {} должен быть числом", name)};
            const double x = args[0].v;
            const double r = name == "asin" ? std::asin(x) : name == "acos" ? std::acos(x) : std::atan(x);
            if (std::isnan(r)) return Error{std::format("Аргумент {} вне области определения", name)};
            return Value{r, {0, 1}};
        }
        if (name == "sqrt") {
            if (Status s = expectArgs(1); !s) return s.GetError();
            const Dim d = args[0].dim;
            if (d.length % 2 != 0 || d.angle % 2 != 0) return Error{"Корень из величины с нечётной степенью единиц"};
            if (args[0].v < 0.0) return Error{"Корень из отрицательного числа"};
            return Value{std::sqrt(args[0].v), {d.length / 2, d.angle / 2}};
        }
        if (name == "abs" || name == "round") {
            if (Status s = expectArgs(1); !s) return s.GetError();
            const double r = name == "abs" ? std::abs(args[0].v) : std::round(args[0].v);
            return Value{r, args[0].dim};
        }
        // min / max
        if (args.empty()) return Error{std::format("Функция {} ждёт хотя бы один аргумент", name)};
        Value best = args[0];
        for (std::size_t i = 1; i < args.size(); ++i) {
            Value a = best;
            Value b = args[i];
            if (a.dim.IsNone() && !b.dim.IsNone()) a = Adopt(a, b.dim);
            if (b.dim.IsNone() && !a.dim.IsNone()) b = Adopt(b, a.dim);
            if (!(a.dim == b.dim)) return Error{std::format("Аргументы {} в разных единицах", name)};
            best = (name == "min") == (b.v < a.v) ? b : a;
        }
        return best;
    }

    std::string ReadWord() {
        const std::size_t start = myPos;
        // The degree sign is a two-byte UTF-8 word on its own.
        if (myText.substr(myPos, 2) == "\xC2\xB0") {
            myPos += 2;
            return std::string(myText.substr(start, 2));
        }
        if (myPos < myText.size() && IsIdentStart(myText[myPos])) {
            while (myPos < myText.size() && IsIdentChar(myText[myPos]) &&
                   myText.substr(myPos, 2) != "\xC2\xB0")
                ++myPos;
        }
        return std::string(myText.substr(start, myPos - start));
    }

    void SkipSpaces() {
        while (myPos < myText.size() && std::isspace(static_cast<unsigned char>(myText[myPos]))) ++myPos;
    }

    char Peek() const { return myPos < myText.size() ? myText[myPos] : '\0'; }

    std::string_view   myText;
    const UnitSystem&  myUnits;
    const NameLookup&  myLookup;
    std::size_t        myPos       = 0;
    int                myDepth     = 0;
    int                myCallDepth = 0;

public:
    /// Plain parts that took the field's unit, in the order they did.
    mutable std::vector<Adoption> myAdopted;
};

} // namespace

Expected<double> EvaluateExpression(std::string_view text, Quantity expected,
                                    const UnitSystem& units, const NameLookup& lookup) {
    if (text.size() > kMaxExpressionLength) return Error{"Слишком длинное выражение"};
    Parser parser(text, units, lookup);
    Expected<Value> result = parser.ParseAll();
    if (!result) return result.GetError();

    Value v = *result;
    const Dim want = DimOf(expected);
    if (v.dim.IsNone() && !want.IsNone()) v = parser.Adopt(v, want);
    if (!(v.dim == want)) {
        switch (expected) {
            case Quantity::Length: return Error{"Ожидалась длина"};
            case Quantity::Angle:  return Error{"Ожидался угол"};
            case Quantity::Scalar: return Error{"Ожидалось число без единиц"};
        }
    }
    if (!std::isfinite(v.v)) return Error{"Результат не является конечным числом"};
    return v.v;
}

namespace {

std::string_view UnitName(LengthUnit u) {
    switch (u) {
        case LengthUnit::Millimeter: return "mm";
        case LengthUnit::Centimeter: return "cm";
        case LengthUnit::Meter:      return "m";
        case LengthUnit::Inch:       return "in";
    }
    return "mm";
}

std::string_view UnitName(AngleUnit u) { return u == AngleUnit::Radian ? "rad" : "deg"; }

/// True when `text` is a single number with nothing after it ("12,5").
bool PlainNumber(std::string_view text) {
    if (text.empty() || !(std::isdigit(static_cast<unsigned char>(text.front())) || text.front() == '.')) return false;
    for (const char c : text)
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != ',' && c != 'e' && c != 'E' && c != '+' && c != '-')
            return false;
    return true;
}

} // namespace

std::string ExpressionInUnits(std::string_view text, Quantity expected, const UnitSystem& from, const UnitSystem& to,
                              const NameLookup& lookup) {
    if (from.length == to.length && from.angle == to.angle) return std::string(text);
    if (text.size() > kMaxExpressionLength) return std::string(text);
    Parser parser(text, from, lookup);
    Expected<Value> result = parser.ParseAll();
    if (!result) return std::string(text);
    Value v = *result;
    const Dim want = DimOf(expected);
    if (v.dim.IsNone() && !want.IsNone()) v = parser.Adopt(v, want);

    // Every adopted part whose unit changes gets the old unit written out:
    // "(" before it and ")*1mm" after it, or just "mm" after a lone number.
    struct Insert {
        std::size_t at;
        long long   order;   // at one place: closings of inner parts first, then openings of outer ones
        std::string text;
    };
    std::vector<Insert> inserts;
    for (const Adoption& a : parser.myAdopted) {
        const bool length = a.dim == Dim{1, 0};
        if (length ? from.length == to.length : from.angle == to.angle) continue;
        const std::string_view unit = length ? UnitName(from.length) : UnitName(from.angle);
        const std::string_view part = text.substr(a.from, a.to - a.from);
        const auto size = static_cast<long long>(a.to - a.from);   // shorter = inner
        if (PlainNumber(part)) {
            inserts.push_back({a.to, -(1LL << 40) + size, std::string(unit)});
        } else {
            inserts.push_back({a.from, (1LL << 40) - size, "("});
            inserts.push_back({a.to, -(1LL << 40) + size, std::format(")*1{}", unit)});
        }
    }
    if (inserts.empty()) return std::string(text);
    std::sort(inserts.begin(), inserts.end(), [](const Insert& x, const Insert& y) {
        return x.at != y.at ? x.at < y.at : x.order < y.order;
    });
    std::string out;
    std::size_t pos = 0;
    for (const Insert& in : inserts) {
        out.append(text.substr(pos, in.at - pos));
        out += in.text;
        pos = in.at;
    }
    out.append(text.substr(pos));
    return out;
}

std::vector<std::string> ReferencedNames(std::string_view text) {
    std::vector<std::string> names;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            // Skip a number together with an attached unit word ("25mm").
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) ++i;
            while (i < text.size() && IsIdentChar(text[i])) ++i;
            continue;
        }
        if (!IsIdentStart(c)) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < text.size() && IsIdentChar(text[i])) ++i;
        const std::string word(text.substr(start, i - start));
        std::size_t j = i;
        while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        const bool isCall = j < text.size() && text[j] == '(';
        if (isCall || FindUnit(word) || word == "pi" || word == "\xC2\xB0") continue;
        bool seen = false;
        for (const auto& n : names) seen = seen || n == word;
        if (!seen) names.push_back(word);
    }
    return names;
}

std::string RenameInExpression(std::string_view text, std::string_view from, std::string_view to) {
    // The same scan as ReferencedNames: what it reports as a name is what is renamed.
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const std::size_t start = i;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) ++i;
            while (i < text.size() && IsIdentChar(text[i])) ++i;   // "25mm", "1e3"
            out.append(text.substr(start, i - start));
            continue;
        }
        if (!IsIdentStart(c)) {
            out.push_back(c);
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < text.size() && IsIdentChar(text[i])) ++i;
        const std::string_view word = text.substr(start, i - start);
        std::size_t j = i;
        while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        const bool isCall = j < text.size() && text[j] == '(';
        out.append(word == from && !isCall ? to : word);
    }
    return out;
}

bool IsValidVariableName(std::string_view name) {
    if (name.empty() || !IsIdentStart(name.front())) return false;
    for (char c : name)
        if (!IsIdentChar(c)) return false;
    return !FindUnit(name) && !IsFunction(name) && name != "pi";
}

} // namespace bz
