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
