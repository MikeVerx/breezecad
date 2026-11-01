#include "sheetmetal/SheetRules.h"

#include <algorithm>
#include <cmath>

namespace bz::sheet {

double DefaultRadius(double thickness) { return thickness; }

double KFactorDin6935(double radius, double thickness) {
    if (thickness <= 0.0) return 0.5;
    const double ratio = radius / thickness;
    if (ratio >= 5.0) return 0.5;
    if (ratio <= 0.0) return 0.0;
    const double k = 0.65 + 0.5 * std::log10(ratio);
    return std::clamp(k / 2.0, 0.0, 0.5);
}

double BendAllowance(double angle, double radius, double kFactor, double thickness) {
    return angle * (radius + kFactor * thickness);
}

double OuterSetback(double angle, double radius, double thickness) { return (radius + thickness) * std::tan(angle / 2.0); }

Rules Rules::Auto(double thickness) {
    Rules r;
    r.thickness = thickness;
    r.radius    = DefaultRadius(thickness);
    r.kFactor   = KFactorDin6935(r.radius, thickness);
    return r;
}

} // namespace bz::sheet
