#pragma once
// Bend rules of a sheet metal part: thickness, inner bend radius, K-factor.
// Only the thickness has to be typed; the rest follows from it unless the
// user overrides it. No geometry kernel here (docs/ADR/0007-sheet-metal.md).

namespace bz::sheet {

/// Inner bend radius used when the user did not type one: equal to the
/// thickness (the usual shop default; Inventor's default rule does the same,
/// not checked against its documentation).
double DefaultRadius(double thickness);

/// K-factor from DIN 6935: correction factor k = 0.65 + 0.5 * lg(r / s) for
/// r / s < 5 and k = 1 beyond; the K-factor (neutral layer at K * s from the
/// inner side) is k / 2. Kept within [0, 0.5]: for very sharp bends
/// (r / s < 0.05) the formula would go below zero.
/// Source of the formula: DIN 6935 (paid), as quoted in CAD documentation;
/// the r / s < 5 limit is not checked against the standard text.
double KFactorDin6935(double radius, double thickness);

/// Length of the neutral layer through a bend, mm: angle * (R + K * s).
/// `angle` is the bend angle in radians (0 = flat, pi/2 = right angle).
double BendAllowance(double angle, double radius, double kFactor, double thickness);

/// Distance from the virtual sharp (where the outer faces would meet) to
/// where the bend starts: (R + s) * tan(angle / 2).
double OuterSetback(double angle, double radius, double thickness);

/// What a part bends with.
struct Rules {
    double thickness = 2.0;   ///< s, mm
    double radius    = 2.0;   ///< inner bend radius R, mm
    double kFactor   = 0.325; ///< neutral layer position, 0..0.5

    /// Rules for `thickness` with everything else automatic.
    static Rules Auto(double thickness);
};

} // namespace bz::sheet
