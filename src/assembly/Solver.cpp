#include "assembly/Solver.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bz::assembly {
namespace {

/// Length that turns radians into millimetres, so rotations and shifts are
/// weighed alike (a degree at 100 mm is about 1.7 mm).
constexpr double kScale = 100.0;
constexpr double kFailed = 1e-4;   ///< mm: a constraint further off than this does not hold

struct WorldGeometry {
    GeometryKind kind;
    Vec3         p, d;
    double       r = 0.0;
};

WorldGeometry ToWorld(const Placement& at, const GeometryRef& g) {
    return {g.kind, at.Point(g.origin), at.Direction(g.direction).Normalized(), g.radius};
}

bool IsPlane(const WorldGeometry& g) { return g.kind == GeometryKind::Plane; }
bool HasAxis(const WorldGeometry& g) {
    return g.kind == GeometryKind::Axis || g.kind == GeometryKind::Cylinder || g.kind == GeometryKind::Circle;
}

void Push(std::vector<double>& out, Vec3 v, double k = 1.0) {
    out.push_back(v.x * k);
    out.push_back(v.y * k);
    out.push_back(v.z * k);
}

/// The part of `v` across direction `d`.
Vec3 Across(Vec3 v, Vec3 d) { return v - d * v.Dot(d); }

/// Two lines on one axis (directions either way).
void Collinear(std::vector<double>& out, const WorldGeometry& a, const WorldGeometry& b) {
    Push(out, a.d.Cross(b.d), kScale);
    Push(out, Across(b.p - a.p, a.d));
}

/// Equations of one constraint; all in millimetres.
void Residuals(const Constraint& c, WorldGeometry a, WorldGeometry b, std::vector<double>& out) {
    const double sign = c.flipped ? -1.0 : 1.0;
    switch (c.type) {
        case ConstraintType::Mate:
            if (IsPlane(a) && IsPlane(b)) {
                // Face to face: normals opposed (flush: the same way), gap `value`.
                Push(out, c.flipped ? a.d - b.d : a.d + b.d, kScale);
                out.push_back((b.p - a.p).Dot(a.d) - c.value);
            } else if (HasAxis(a) && HasAxis(b)) {
                Collinear(out, a, b);
            } else if (IsPlane(a) || IsPlane(b)) {
                if (IsPlane(b)) std::swap(a, b);
                if (HasAxis(b)) out.push_back(a.d.Dot(b.d) * kScale);   // an axis lies in the plane
                out.push_back((b.p - a.p).Dot(a.d) - c.value);
            } else {
                Push(out, b.p - a.p);   // two points
            }
            break;
        case ConstraintType::Insert:
            // A bolt into a hole: one axis, the circles' planes `value` apart.
            Push(out, c.flipped ? a.d - b.d : a.d + b.d, kScale);
            Push(out, Across(b.p - a.p, a.d));
            out.push_back((b.p - a.p).Dot(a.d) - c.value);
            break;
        case ConstraintType::AxisMate:
            Collinear(out, a, b);
            break;
        case ConstraintType::Angle:
            out.push_back((a.d.Dot(b.d) - std::cos(c.value)) * kScale);
            break;
        case ConstraintType::Tangent: {
            if (IsPlane(b) || (b.kind == GeometryKind::Cylinder && a.kind == GeometryKind::Sphere)) std::swap(a, b);
            if (IsPlane(a)) {
                // A cylinder or sphere touching a plane from the side its normal points to.
                if (b.kind == GeometryKind::Cylinder) out.push_back(a.d.Dot(b.d) * kScale);
                out.push_back((b.p - a.p).Dot(a.d) - sign * b.r - c.value);
            } else if (a.kind == GeometryKind::Cylinder && b.kind == GeometryKind::Cylinder) {
                Push(out, a.d.Cross(b.d), kScale);
                const double touching = c.flipped ? std::abs(a.r - b.r) : a.r + b.r;
                out.push_back(Across(b.p - a.p, a.d).Length() - touching - c.value);
            } else if (a.kind == GeometryKind::Cylinder) {   // and a sphere
                const double touching = c.flipped ? std::abs(a.r - b.r) : a.r + b.r;
                out.push_back(Across(b.p - a.p, a.d).Length() - touching - c.value);
            } else {   // two spheres
                const double touching = c.flipped ? std::abs(a.r - b.r) : a.r + b.r;
                out.push_back((b.p - a.p).Length() - touching - c.value);
            }
            break;
        }
    }
}

// --- small dense linear algebra --------------------------------------------------

using Matrix = std::vector<std::vector<double>>;

/// Solves A x = b by Gaussian elimination with partial pivoting (A is
/// symmetric positive definite here, damped). False when singular.
bool SolveLinear(Matrix a, std::vector<double> b, std::vector<double>& x) {
    const std::size_t n = b.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        if (std::abs(a[pivot][col]) < 1e-300) return false;
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = a[r][col] / a[col][col];
            if (f == 0.0) continue;
            for (std::size_t k = col; k < n; ++k) a[r][k] -= f * a[col][k];
            b[r] -= f * b[col];
        }
    }
    x.assign(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double s = b[i];
        for (std::size_t k = i + 1; k < n; ++k) s -= a[i][k] * x[k];
        x[i] = s / a[i][i];
    }
    return true;
}

/// Numerical rank (full pivoting) of the columns `cols` of `m`.
int Rank(const Matrix& m, const std::vector<std::size_t>& cols) {
    if (m.empty() || cols.empty()) return 0;
    Matrix a(m.size(), std::vector<double>(cols.size()));
    double biggest = 0.0;
    for (std::size_t r = 0; r < m.size(); ++r)
        for (std::size_t c = 0; c < cols.size(); ++c) {
            a[r][c] = m[r][cols[c]];
            biggest = std::max(biggest, std::abs(a[r][c]));
        }
    if (biggest == 0.0) return 0;
    const double tol = biggest * 1e-7;
    int rank = 0;
    std::vector<bool> usedRow(a.size(), false), usedCol(cols.size(), false);
    for (;;) {
        double best = tol;
        std::size_t br = 0, bc = 0;
        bool found = false;
        for (std::size_t r = 0; r < a.size(); ++r) {
            if (usedRow[r]) continue;
            for (std::size_t c = 0; c < cols.size(); ++c)
                if (!usedCol[c] && std::abs(a[r][c]) > best) {
                    best = std::abs(a[r][c]);
                    br = r;
                    bc = c;
                    found = true;
                }
        }
        if (!found) return rank;
        ++rank;
        usedRow[br] = usedCol[bc] = true;
        for (std::size_t r = 0; r < a.size(); ++r) {
            if (usedRow[r]) continue;
            const double f = a[r][bc] / a[br][bc];
            for (std::size_t c = 0; c < cols.size(); ++c) a[r][c] -= f * a[br][c];
        }
    }
}

// --- the system ---------------------------------------------------------------------

/// The unknowns: six per free component, measured from where it started.
struct System {
    Assembly&                    assembly;
    std::vector<const Constraint*> active;
    std::vector<ObjectId>        free;         ///< components with unknowns
    std::vector<Placement>       start;        ///< their placements at the start
    std::vector<double>          weight;       ///< how readily each moves (1; less for the kept one)
    std::unordered_map<ObjectId, std::size_t> index;

    Placement PoseOf(std::size_t i, const std::vector<double>& y) const {
        const double* v = &y[6 * i];
        const double s = weight[i];
        const Vec3 shift{v[0] * s, v[1] * s, v[2] * s};
        const Vec3 turn{v[3] * s / kScale, v[4] * s / kScale, v[5] * s / kScale};
        return {Rotation::FromVector(turn) * start[i].rotation, start[i].translation + shift};
    }

    Placement PlacementOf(ObjectId id, const std::vector<double>& y) const {
        if (const auto it = index.find(id); it != index.end()) return PoseOf(it->second, y);
        const Component* c = assembly.FindComponent(id);
        return c ? c->placement : Placement{};
    }

    /// Residuals of all active constraints; `rows` gets where each starts.
    std::vector<double> F(const std::vector<double>& y, std::vector<std::size_t>* rows = nullptr) const {
        std::vector<double> out;
        if (rows) rows->clear();
        for (const Constraint* c : active) {
            if (rows) rows->push_back(out.size());
            Residuals(*c, ToWorld(PlacementOf(c->first.component, y), c->first),
                      ToWorld(PlacementOf(c->second.component, y), c->second), out);
        }
        if (rows) rows->push_back(out.size());
        return out;
    }

    Matrix J(const std::vector<double>& y, const std::vector<double>& f0) const {
        Matrix j(f0.size(), std::vector<double>(y.size(), 0.0));
        constexpr double h = 1e-6;
        std::vector<double> yh = y;
        for (std::size_t k = 0; k < y.size(); ++k) {
            yh[k] = y[k] + h;
            const std::vector<double> f1 = F(yh);
            yh[k] = y[k];
            for (std::size_t r = 0; r < f0.size(); ++r) j[r][k] = (f1[r] - f0[r]) / h;
        }
        return j;
    }
};

double Norm(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v) s += x * x;
    return std::sqrt(s);
}

System MakeSystem(Assembly& a, ObjectId keep) {
    System s{a, {}, {}, {}, {}, {}};
    for (const Constraint& c : a.constraints) {
        if (c.suppressed || !a.FindComponent(c.first.component) || !a.FindComponent(c.second.component)) continue;
        s.active.push_back(&c);
        for (ObjectId id : {c.first.component, c.second.component}) {
            const Component* comp = a.FindComponent(id);
            if (comp->grounded || s.index.contains(id)) continue;
            s.index[id] = s.free.size();
            s.free.push_back(id);
            s.start.push_back(comp->placement);
            s.weight.push_back(id == keep ? 0.05 : 1.0);
        }
    }
    return s;
}

} // namespace

double ConstraintError(const Assembly& assembly, const Constraint& c) {
    const Component* a = assembly.FindComponent(c.first.component);
    const Component* b = assembly.FindComponent(c.second.component);
    if (!a || !b) return 0.0;
    std::vector<double> out;
    Residuals(c, ToWorld(a->placement, c.first), ToWorld(b->placement, c.second), out);
    return Norm(out);
}

SolveResult Solve(Assembly& a, const SolveOptions& options) {
    SolveResult result;
    for (Constraint& c : a.constraints) {
        c.state = c.suppressed ? ConstraintState::Unknown : ConstraintState::Satisfied;
        c.error = 0.0;
    }
    System s = MakeSystem(a, options.keep);
    if (s.active.empty()) {
        result.converged = true;
        return result;
    }

    std::vector<double> y(6 * s.free.size(), 0.0);
    std::vector<double> f = s.F(y);
    double err = Norm(f);
    double lambda = 1e-3;
    while (!y.empty() && err > options.tolerance && result.iterations < options.maxIterations) {
        ++result.iterations;
        const Matrix j = s.J(y, f);
        const std::size_t n = y.size();
        // Normal equations, damped: (JᵀJ + λI) dy = -Jᵀf. With λ small the step
        // is the shortest one that removes the error.
        Matrix jtj(n, std::vector<double>(n, 0.0));
        std::vector<double> jtf(n, 0.0);
        for (std::size_t r = 0; r < f.size(); ++r)
            for (std::size_t p = 0; p < n; ++p) {
                if (j[r][p] == 0.0) continue;
                jtf[p] -= j[r][p] * f[r];
                for (std::size_t q = 0; q < n; ++q) jtj[p][q] += j[r][p] * j[r][q];
            }
        bool improved = false;
        for (int tries = 0; tries < 12 && !improved; ++tries) {
            Matrix damped = jtj;
            for (std::size_t p = 0; p < n; ++p) damped[p][p] += lambda * (1.0 + jtj[p][p]);
            std::vector<double> dy;
            if (!SolveLinear(damped, jtf, dy)) {
                lambda *= 10;
                continue;
            }
            std::vector<double> y1 = y;
            for (std::size_t p = 0; p < n; ++p) y1[p] += dy[p];
            std::vector<double> f1 = s.F(y1);
            const double e1 = Norm(f1);
            if (e1 < err) {
                y = std::move(y1);
                f = std::move(f1);
                err = e1;
                lambda = std::max(lambda * 0.2, 1e-12);
                improved = true;
            } else {
                lambda *= 8;
            }
        }
        if (!improved) break;   // a minimum that is not a solution: the constraints contradict
    }

    // Take the result.
    for (std::size_t i = 0; i < s.free.size(); ++i) {
        Component* c = a.FindComponent(s.free[i]);
        const Placement p = s.PoseOf(i, y);
        Placement next{p.rotation.Orthonormalized(), p.translation};
        const bool changed = (next.translation - c->placement.translation).Length() > 1e-12 ||
                             (next.rotation.col[0] - c->placement.rotation.col[0]).Length() > 1e-12 ||
                             (next.rotation.col[2] - c->placement.rotation.col[2]).Length() > 1e-12;
        if (changed) {
            c->placement = next;
            result.moved = true;
        }
    }
    result.error = err;
    result.converged = err <= options.tolerance * 10;

    // States: which do not hold, which repeat what others say.
    // Against the final placements (the system above started from the old ones).
    std::vector<std::size_t> rows;
    System fin = MakeSystem(a, {});
    std::vector<double> y0(6 * fin.free.size(), 0.0);
    const std::vector<double> f0 = fin.F(y0, &rows);
    const Matrix j = y0.empty() ? Matrix(f0.size()) : fin.J(y0, f0);
    std::vector<std::size_t> allCols(y0.size());
    for (std::size_t k = 0; k < allCols.size(); ++k) allCols[k] = k;
    // Rank of the equations of the first `end` rows.
    const auto rankOfRows = [&](std::size_t end) {
        return Rank(Matrix(j.begin(), j.begin() + static_cast<std::ptrdiff_t>(end)), allCols);
    };
    for (std::size_t k = 0; k < fin.active.size(); ++k) {
        auto* c = const_cast<Constraint*>(fin.active[k]);
        std::vector<double> own(f0.begin() + static_cast<std::ptrdiff_t>(rows[k]),
                                f0.begin() + static_cast<std::ptrdiff_t>(rows[k + 1]));
        c->error = Norm(own);
        if (c->error > kFailed) {
            c->state = ConstraintState::Failed;
            continue;
        }
        // Redundant: it holds and adds nothing to the constraints before it.
        if (y0.empty()) continue;
        if (rankOfRows(rows[k + 1]) == rankOfRows(rows[k])) c->state = ConstraintState::Redundant;
    }
    return result;
}

std::unordered_map<ObjectId, Freedom> AnalyzeFreedom(const Assembly& a) {
    std::unordered_map<ObjectId, Freedom> out;
    auto& mutableAssembly = const_cast<Assembly&>(a);   // the system only reads it here
    const System s = MakeSystem(mutableAssembly, {});
    for (const Component& c : a.components) out[c.id] = c.grounded ? Freedom{0, 0} : Freedom{3, 3};
    if (s.free.empty()) return out;
    const std::vector<double> y(6 * s.free.size(), 0.0);
    const std::vector<double> f = s.F(y);
    const Matrix j = s.J(y, f);
    for (std::size_t i = 0; i < s.free.size(); ++i) {
        // Freedom of this one with the others held where they are.
        const std::vector<std::size_t> shift{6 * i, 6 * i + 1, 6 * i + 2};
        const std::vector<std::size_t> all{6 * i, 6 * i + 1, 6 * i + 2, 6 * i + 3, 6 * i + 4, 6 * i + 5};
        const int shiftRank = Rank(j, shift);
        const int rank = Rank(j, all);
        out[s.free[i]] = {3 - shiftRank, 3 - (rank - shiftRank)};
    }
    return out;
}

} // namespace bz::assembly
