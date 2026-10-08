#pragma once

#include "core/Units.h"

#include <string>

namespace bz {

/// A named, user-editable numeric value: a variable from the variables
/// table, a sketch dimension ("d3") or an operation input ("distance").
/// The expression is the source of truth; `value` is its cached result.
struct Parameter {
    std::string name;                     ///< unique within a document, e.g. "d1"
    std::string expression;               ///< what the user typed, e.g. "w / 2"
    Quantity    quantity = Quantity::Length;
    double      value    = 0.0;           ///< evaluated, internal units (mm / rad)
    std::string error;                    ///< non-empty when evaluation failed

    bool HasError() const { return !error.empty(); }
};

} // namespace bz
