#pragma once
// Internal to the sketch solver. Turns a Sketch into unknowns and residual
// equations; Solver.cpp does the numerics.

#include "sketch/Sketch.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

namespace bz::sketch::detail {

/// Shape of one residual function f(x) = 0. Indices refer to the unknown
/// vector; see Residual() for the exact formula of each kind.
enum class EqKind : unsigned char {
    Linear,          ///< sum(k[i] * x[v[i]]) - value
    PointDistance,   ///< |P - Q| - value                    P=(v0,v1) Q=(v2,v3)
    PointDistanceVar,///< |P - Q| - x[v4]
    PointLine,       ///< sign * cross(B - A, P - A) / |B - A| - value     P=(v0,v1) A=(v2,v3) B=(v4,v5)
    PointLineVar,    ///< sign * cross(B - A, P - A) / |B - A| - x[v6]
    Parallel,        ///< cross(D1, D2) / (|D1||D2|)          D1 = (v2,v3)-(v0,v1), D2 = (v6,v7)-(v4,v5)
    Perpendicular,   ///< dot(D1, D2) / (|D1||D2|)
    EqualLength,     ///< |D1| - |D2|
    Angle,           ///< wrap(atan2(cross(D1,D2), dot(D1,D2)) - value)
    CircleTangent,   ///< |C1 - C2| - (k0 * x[v4] + k1 * x[v5])    C1=(v0,v1) C2=(v2,v3)
    SymmetricMid,    ///< cross(B - A, (P + Q) / 2 - A) / |B - A|  P=(v0,v1) Q=(v2,v3) A=(v4,v5) B=(v6,v7)
    SymmetricPerp,   ///< dot(Q - P, B - A) / |B - A|
    PointSpline,     ///< signed distance from P=(v0,v1) to the spline through `curve`
    PointEllipse,    ///< ((u/a)^2 + (v/b)^2 - 1) * min(a,b)/2   P=(v0,v1) C=(v2,v3) major end=(v4,v5) minor end=(v6,v7)
    SplineEndTangent,///< sin of the angle between the spline's end tangent (`curve`, `end`) and:
                     ///<   other 0: line (v0..v3); 1: the radius from centre (v0,v1) to the end; 2: the end tangent of `curve2`
    LineEllipseTangent, ///< line A=(v0,v1) B=(v2,v3) touches the ellipse C=(v4,v5) major=(v6,v7) minor=(v8,v9)
};

struct Equation {
    EqKind                kind = EqKind::Linear;
    std::array<int, 10>   v{};       ///< unknown indices
    int                   count = 0; ///< used entries of v
    std::array<double, 4> k{};       ///< Linear and CircleTangent coefficients
    double                value = 0.0;
    double                sign  = 1.0;
    ObjectId              source;    ///< constraint that produced it (none for internal ones)
    // Splines: first unknown of each of their points, in order.
    std::vector<int>      curve, curve2;
    bool                  interpolate = true, interpolate2 = true;
    bool                  closed = false;
    int                   end = 0, end2 = 0;   ///< 0: the start, 1: the end
    int                   other = 0;           ///< SplineEndTangent: what the spline is tangent to

    /// Every unknown the residual reads, each once.
    std::vector<int> Unknowns() const {
        std::vector<int> out(v.begin(), v.begin() + count);
        for (const auto* c : {&curve, &curve2})
            for (const int i : *c) {
                out.push_back(i);
                out.push_back(i + 1);
            }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
};

/// Unknowns and equations of one sketch.
class EquationSystem {
public:
    /// Builds the system. `skip` excludes one constraint (used to test
    /// redundancy); `extra` adds a candidate constraint that is not yet in
    /// the sketch.
    EquationSystem(const Sketch& sketch, ObjectId skip = {}, const Constraint* extra = nullptr);

    std::vector<double>          x;        ///< current values of all unknowns
    std::vector<bool>            isFree;   ///< false for fixed unknowns (origin, Fix)
    std::vector<Equation>        equations;

    /// First unknown of a point (x; y follows) or the radius of a circle/arc.
    std::unordered_map<ObjectId, int> indexOf;

    double Residual(const Equation& eq, const std::vector<double>& values) const;

    /// Writes unknowns back into the sketch.
    void Apply(Sketch& sketch) const;

    /// Makes a point's unknowns fixed at `pos` (drag target).
    void Pin(ObjectId point, Vec2 pos);

    /// Unknown indices that belong to an entity (for "fully defined").
    std::vector<int> UnknownsOf(const Sketch& sketch, ObjectId entity) const;

private:
    void AddConstraint(const Sketch& sketch, const Constraint& c);
    void Add(Equation eq) { equations.push_back(eq); }
    int  PointIndex(ObjectId id) const;
    int  RadiusIndex(ObjectId id) const;
};

} // namespace bz::sketch::detail
