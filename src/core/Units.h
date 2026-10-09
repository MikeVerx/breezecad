#pragma once

#include <optional>
#include <string_view>

namespace bz {

/// Physical meaning of a numeric value.
enum class Quantity : unsigned char { Scalar, Length, Angle };

/// Internally every length is in millimetres and every angle in radians
/// (ADR-0003). Display units only affect input and output.
enum class LengthUnit : unsigned char { Millimeter, Centimeter, Meter, Inch };
enum class AngleUnit : unsigned char { Degree, Radian };

/// Millimetres in one `unit`.
double MillimetersPer(LengthUnit unit);

/// Stable ASCII identifiers used in files: "mm", "cm", "m", "in", "deg", "rad".
std::string_view ToId(LengthUnit unit);
std::string_view ToId(AngleUnit unit);
std::optional<LengthUnit> LengthUnitFromId(std::string_view id);
std::optional<AngleUnit>  AngleUnitFromId(std::string_view id);

/// Units the user works in. Stored per document.
struct UnitSystem {
    LengthUnit length = LengthUnit::Millimeter;
    AngleUnit  angle  = AngleUnit::Degree;

    /// Display value -> internal value (mm or rad).
    double ToInternal(double display, Quantity quantity) const;
    /// Internal value (mm or rad) -> display value.
    double ToDisplay(double internal, Quantity quantity) const;

    bool operator==(const UnitSystem&) const = default;
};

} // namespace bz
