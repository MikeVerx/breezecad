#pragma once

#include "core/Units.h"

#include <string>

namespace bz::app {

/// "12.5", "60", "0.333" — at most `decimals` digits, no trailing zeros,
/// decimal point (expressions accept both point and comma).
std::string FormatNumber(double value, int decimals = 3);

/// Display value of an internal length/angle in the document units.
std::string FormatValue(double internal, Quantity quantity, const UnitSystem& units, int decimals = 3);

/// Short unit label for fields: "мм", "см", "м", "дюйм", "°", "рад".
std::string UnitLabel(Quantity quantity, const UnitSystem& units);

} // namespace bz::app
