#pragma once

#include "core/Expected.h"
#include "core/Units.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bz {

/// A value bound to a name that expressions may reference
/// (a user variable or a named dimension).
struct NamedValue {
    double   value    = 0.0;            ///< internal units (mm / rad)
    Quantity quantity = Quantity::Scalar;
};

/// Looks a name up; returns nullopt when the name is unknown.
using NameLookup = std::function<std::optional<NamedValue>(std::string_view)>;

/// Evaluates text typed into a numeric field, e.g. "25", "12,5",
/// "d1 * 2 + 3mm", "atan(h / w)", "1in".
///
/// Rules:
///  - A number without a unit takes the document unit of the field
///    (`units` and `expected`): "25" in a length field of a document in
///    millimetres is 25 mm, in inches it is 25 in.
///  - Units may follow numbers: mm cm m in deg ° rad.
///  - Dimensions are checked: "d1 + 5deg" for a length d1 is an error.
///  - A comma between digits is a decimal separator ("12,5"); inside
///    function calls arguments are separated by ';' or ','.
///  - Functions: sin cos tan asin acos atan sqrt abs min max round; constant pi.
///
/// Longer text is refused ("Слишком длинное выражение"), as is nesting
/// deeper than a couple of hundred levels.
inline constexpr std::size_t kMaxExpressionLength = 4096;

/// Returns the value in internal units (mm / rad / plain number).
Expected<double> EvaluateExpression(std::string_view text,
                                    Quantity expected,
                                    const UnitSystem& units,
                                    const NameLookup& lookup);

/// Identifiers referenced by an expression, in order of appearance,
/// without duplicates and without function names or units. Used to build
/// parameter dependencies and detect cycles.
std::vector<std::string> ReferencedNames(std::string_view text);

/// True for a valid user variable name: a letter or '_' followed by
/// letters, digits or '_'; not a unit or function name.
bool IsValidVariableName(std::string_view name);

} // namespace bz
