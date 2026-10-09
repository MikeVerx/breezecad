#include "app/Format.h"

#include <cmath>
#include <format>

namespace bz::app {

std::string FormatNumber(double value, int decimals) {
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) value = 0.0;   // no "-0"
    std::string s = std::format("{:.{}f}", value, decimals);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

std::string FormatGrouped(double value, int decimals) {
    std::string s = FormatNumber(value, decimals);
    const std::size_t sign = !s.empty() && s[0] == '-' ? 1 : 0;
    std::size_t point = s.find('.');
    if (point == std::string::npos) point = s.size();
    for (std::size_t i = point; i > sign + 3; i -= 3) s.insert(i - 3, " ");
    return s;
}

std::string FormatValue(double internal, Quantity quantity, const UnitSystem& units, int decimals) {
    return FormatNumber(units.ToDisplay(internal, quantity), decimals);
}

std::string UnitLabel(Quantity quantity, const UnitSystem& units) {
    if (quantity == Quantity::Angle) return units.angle == AngleUnit::Degree ? "\xC2\xB0" : "рад";
    if (quantity == Quantity::Scalar) return {};
    switch (units.length) {
        case LengthUnit::Millimeter: return "мм";
        case LengthUnit::Centimeter: return "см";
        case LengthUnit::Meter:      return "м";
        case LengthUnit::Inch:       return "дюйм";
    }
    return "мм";
}

} // namespace bz::app
