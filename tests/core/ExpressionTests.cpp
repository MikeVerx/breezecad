#include "core/Expression.h"
#include "core/Math.h"

#include <doctest.h>

#include <map>

using namespace bz;

namespace {

const UnitSystem kMm{LengthUnit::Millimeter, AngleUnit::Degree};
const UnitSystem kInch{LengthUnit::Inch, AngleUnit::Degree};

double Eval(std::string_view text, Quantity q = Quantity::Length, const UnitSystem& units = kMm,
            const NameLookup& lookup = {}) {
    Expected<double> r = EvaluateExpression(text, q, units, lookup);
    REQUIRE_MESSAGE(r.HasValue(), (r.HasValue() ? "" : r.GetError().message));
    return *r;
}

bool Fails(std::string_view text, Quantity q = Quantity::Length, const NameLookup& lookup = {}) {
    return !EvaluateExpression(text, q, kMm, lookup).HasValue();
}

} // namespace

TEST_CASE("plain numbers take the document unit") {
    CHECK(Eval("25") == doctest::Approx(25.0));
    CHECK(Eval("25", Quantity::Length, kInch) == doctest::Approx(635.0));
    CHECK(Eval("90", Quantity::Angle) == doctest::Approx(kPi / 2));
}

TEST_CASE("decimal comma and explicit units") {
    CHECK(Eval("12,5") == doctest::Approx(12.5));
    CHECK(Eval("1in") == doctest::Approx(25.4));
    CHECK(Eval("2 cm + 5") == doctest::Approx(25.0));
    CHECK(Eval("45\xC2\xB0", Quantity::Angle) == doctest::Approx(kPi / 4));
    CHECK(Eval("1rad", Quantity::Angle) == doctest::Approx(1.0));
}

TEST_CASE("arithmetic, functions and precedence") {
    CHECK(Eval("2 + 3 * 4") == doctest::Approx(14.0));
    CHECK(Eval("(2 + 3) * 4") == doctest::Approx(20.0));
    CHECK(Eval("2^3") == doctest::Approx(8.0));
    CHECK(Eval("-5 + 10") == doctest::Approx(5.0));
    CHECK(Eval("10 * sin(30)") == doctest::Approx(5.0));
    CHECK(Eval("max(3; 7, 5)") == doctest::Approx(7.0));
    CHECK(Eval("sqrt(16mm * 4mm)") == doctest::Approx(8.0));
    CHECK(Eval("atan(1)", Quantity::Angle) == doctest::Approx(kPi / 4));
}

TEST_CASE("variables keep their dimension") {
    const std::map<std::string, NamedValue> vars{
        {"w", {100.0, Quantity::Length}},
        {"a", {kPi / 6, Quantity::Angle}},
        {"n", {4.0, Quantity::Scalar}},
    };
    const NameLookup lookup = [&](std::string_view name) -> std::optional<NamedValue> {
        const auto it = vars.find(std::string(name));
        if (it == vars.end()) return std::nullopt;
        return it->second;
    };
    CHECK(Eval("w / 2", Quantity::Length, kMm, lookup) == doctest::Approx(50.0));
    CHECK(Eval("w / n + 5", Quantity::Length, kMm, lookup) == doctest::Approx(30.0));
    CHECK(Eval("w * sin(a)", Quantity::Length, kMm, lookup) == doctest::Approx(50.0));
    CHECK(Eval("w / 10mm", Quantity::Scalar, kMm, lookup) == doctest::Approx(10.0));
    CHECK(Fails("w + a", Quantity::Length, lookup));
    CHECK(Fails("w", Quantity::Angle, lookup));
    CHECK(Fails("q * 2", Quantity::Length, lookup));
}

TEST_CASE("errors are reported, not thrown") {
    CHECK(Fails(""));
    CHECK(Fails("2 +"));
    CHECK(Fails("(2"));
    CHECK(Fails("1 / 0"));
    CHECK(Fails("foo(2)"));
    CHECK(Fails("3 mm mm"));
}

TEST_CASE("referenced names skip functions, units and numbers") {
    const auto names = ReferencedNames("w/2 + sin(a) * 3mm + w + d_1");
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "w");
    CHECK(names[1] == "a");
    CHECK(names[2] == "d_1");
}

TEST_CASE("variable names") {
    CHECK(IsValidVariableName("width"));
    CHECK(IsValidVariableName("d_12"));
    CHECK_FALSE(IsValidVariableName("12d"));
    CHECK_FALSE(IsValidVariableName("mm"));
    CHECK_FALSE(IsValidVariableName("sin"));
    CHECK_FALSE(IsValidVariableName("a b"));
}

TEST_CASE("B-10: absurdly nested or long expressions are refused, not a stack overflow") {
    // Audit 2026-10-13: these overflowed the stack of the recursive parser.
    const std::size_t n = 4000;   // within kMaxExpressionLength once doubled
    CHECK(Fails(std::string(n / 2, '(') + "1" + std::string(n / 2, ')')));
    CHECK(Fails(std::string(n, '-') + "1"));
    std::string power = "2";
    for (int i = 0; i < 1000; ++i) power += "^1";
    CHECK(Fails(power));
    CHECK(Fails(std::string(200000, '(') + "1" + std::string(200000, ')')));
    CHECK(Fails(std::string(kMaxExpressionLength + 1, '1')));
    CHECK(ReferencedNames(std::string(200000, '(') + "a" + std::string(200000, ')')) ==
          std::vector<std::string>{"a"});

    // Ordinary nesting still works.
    CHECK(Eval(std::string(50, '(') + "1" + std::string(50, ')')) == doctest::Approx(1.0));
    CHECK(Eval("--2") == doctest::Approx(2.0));
    CHECK(Eval("2^3^2") == doctest::Approx(512.0));
}

TEST_CASE("renaming in an expression touches whole names only") {
    CHECK(RenameInExpression("d1 * 2 + d10 - d1d1", "d1", "w") == "w * 2 + d10 - d1d1");
    CHECK(RenameInExpression("5d1 + d1", "d1", "w") == "5d1 + w");          // "5d1" is a number with junk, left alone
    CHECK(RenameInExpression("max(d1;d1)", "d1", "ширина") == "max(ширина;ширина)");
    CHECK(RenameInExpression("ширина/2", "ширина", "w") == "w/2");
    CHECK(RenameInExpression("mm + 3mm", "mm", "x") == "x + 3mm");         // a unit after a number stays
    CHECK(RenameInExpression("sin (a) + sin", "sin", "x") == "sin (a) + x"); // a call stays
    CHECK(RenameInExpression("12,5", "d1", "w") == "12,5");
    CHECK(RenameInExpression("", "d1", "w").empty());
}

TEST_CASE("expressions rewritten for other document units mean the same") {
    const UnitSystem mm{};
    UnitSystem inch = mm;
    inch.length = LengthUnit::Inch;
    UnitSystem rad = mm;
    rad.angle = AngleUnit::Radian;
    const NameLookup look = [](std::string_view n) -> std::optional<NamedValue> {
        if (n == "w") return NamedValue{60, Quantity::Length};
        if (n == "k") return NamedValue{2, Quantity::Scalar};
        if (n == "a") return NamedValue{kPi / 6, Quantity::Angle};
        return std::nullopt;
    };
    struct Case { const char* text; Quantity q; const UnitSystem* to; const char* expected; };
    const Case cases[] = {
        {"10", Quantity::Length, &inch, "10mm"},
        {"12,5", Quantity::Length, &inch, "12,5mm"},
        {"10mm", Quantity::Length, &inch, "10mm"},
        {"w / 2", Quantity::Length, &inch, "w / 2"},
        {"w + 5", Quantity::Length, &inch, "w + 5mm"},
        {"5 + w", Quantity::Length, &inch, "5mm + w"},
        {"(10+5)*2", Quantity::Length, &inch, "((10+5)*2)*1mm"},
        {"k*10", Quantity::Length, &inch, "(k*10)*1mm"},
        {"max(10; w)", Quantity::Length, &inch, "max(10mm; w)"},
        {"-10", Quantity::Length, &inch, "(-10)*1mm"},
        {"90", Quantity::Angle, &rad, "90deg"},
        {"atan(1) + 5", Quantity::Angle, &rad, "atan(1) + 5deg"},
        {"sin(30) * w", Quantity::Length, &rad, "sin(30deg) * w"},
        {"3", Quantity::Scalar, &inch, "3"},
        {"10", Quantity::Length, &mm, "10"},
        {"2 +", Quantity::Length, &inch, "2 +"},   // cannot be evaluated: left as it was
    };
    for (const Case& c : cases) {
        CAPTURE(c.text);
        const std::string out = ExpressionInUnits(c.text, c.q, mm, *c.to, look);
        CHECK(out == c.expected);
        const auto before = EvaluateExpression(c.text, c.q, mm, look);
        const auto after = EvaluateExpression(out, c.q, *c.to, look);
        CHECK(before.HasValue() == after.HasValue());
        if (before && after) CHECK(*after == doctest::Approx(*before));
    }
}
