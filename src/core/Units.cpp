#include "core/Units.h"

#include "core/Math.h"

namespace bz {

double MillimetersPer(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter: return 1.0;
        case LengthUnit::Centimeter: return 10.0;
        case LengthUnit::Meter:      return 1000.0;
        case LengthUnit::Inch:       return 25.4;
    }
    return 1.0;
}

std::string_view ToId(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimeter: return "mm";
        case LengthUnit::Centimeter: return "cm";
        case LengthUnit::Meter:      return "m";
        case LengthUnit::Inch:       return "in";
    }
    return "mm";
}

std::string_view ToId(AngleUnit unit) {
    return unit == AngleUnit::Radian ? "rad" : "deg";
}

std::optional<LengthUnit> LengthUnitFromId(std::string_view id) {
    if (id == "mm") return LengthUnit::Millimeter;
    if (id == "cm") return LengthUnit::Centimeter;
    if (id == "m")  return LengthUnit::Meter;
    if (id == "in") return LengthUnit::Inch;
    return std::nullopt;
}

std::optional<AngleUnit> AngleUnitFromId(std::string_view id) {
    if (id == "deg") return AngleUnit::Degree;
    if (id == "rad") return AngleUnit::Radian;
    return std::nullopt;
}

double UnitSystem::ToInternal(double display, Quantity quantity) const {
    switch (quantity) {
        case Quantity::Length: return display * MillimetersPer(length);
        case Quantity::Angle:  return angle == AngleUnit::Degree ? display * kPi / 180.0 : display;
        case Quantity::Scalar: return display;
    }
    return display;
}

double UnitSystem::ToDisplay(double internal, Quantity quantity) const {
    switch (quantity) {
        case Quantity::Length: return internal / MillimetersPer(length);
        case Quantity::Angle:  return angle == AngleUnit::Degree ? internal * 180.0 / kPi : internal;
        case Quantity::Scalar: return internal;
    }
    return internal;
}

} // namespace bz
