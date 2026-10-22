#include "assembly/Solver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
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
    Vec3         x;   ///< joints: the frame's X
};

WorldGeometry ToWorld(const Placement& at, const GeometryRef& g) {
    return {g.kind, at.Point(g.origin), at.Direction(g.direction).Normalized(), g.radius,
            at.Direction(JointFrameX(g)).Normalized()};
}

/// An angle brought into (-π, π].
double Wrap(double a) {
    a = std::remainder(a, 2 * kPi);
    return a <= -kPi ? a + 2 * kPi : a;
}

/// The coordinate of a one-coordinate joint between frames a and b.
double Coordinate(ConstraintType type, const WorldGeometry& a, const WorldGeometry& b) {
    if (MotionOf(type) == JointMotion::Slide) return (b.p - a.p).Dot(a.d);
    const Vec3 y = a.d.Cross(a.x);
    return std::atan2(b.x.Dot(y), b.x.Dot(a.x));
}

/// How far `q` is outside the limits (signed: past `upper` positive), 0 inside.
double OutsideLimits(JointMotion motion, const JointLimits& l, double q) {
    if (motion == JointMotion::Slide) return q < l.lower ? q - l.lower : q > l.upper ? q - l.upper : 0.0;
    // Angles go round: from `lower`, `range` forward is inside.
    const double range = std::clamp(l.upper - l.lower, 0.0, 2 * kPi);
    double from = std::fmod(q - l.lower, 2 * kPi);
    if (from < 0) from += 2 * kPi;
    if (from <= range) return 0.0;
    const double past = from - range, before = 2 * kPi - from;
    return past < before ? past : -before;
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

/// A distance that must be `c.value`, or within the constraint's range.
double DistanceRow(const Constraint& c, double d) {
    if (HasRange(c.type) && c.limits.enabled) return OutsideLimits(JointMotion::Slide, c.limits, d);
    return d - c.value;
}

/// Equations of one geometric constraint; all in millimetres. `s`: the
/// plane of symmetry (Symmetry only). `limits`: with the rows that keep
/// joints within their limits (not when counting freedom: a limit does not
/// take a freedom away).
void Residuals(const Constraint& c, WorldGeometry a, WorldGeometry b, const WorldGeometry* s, std::vector<double>& out,
               bool limits) {
    const double sign = c.flipped ? -1.0 : 1.0;
    const Vec3   facing = c.flipped ? a.d - b.d : a.d + b.d;   // joints: Z opposed, or the same way
    switch (c.type) {
        case ConstraintType::Mate:
            if (IsPlane(a) && IsPlane(b)) {
                // Face to face: normals opposed (flush: the same way), gap `value`.
                Push(out, c.flipped ? a.d - b.d : a.d + b.d, kScale);
                out.push_back(DistanceRow(c, (b.p - a.p).Dot(a.d)));
            } else if (HasAxis(a) && HasAxis(b)) {
                Collinear(out, a, b);
            } else if (IsPlane(a) || IsPlane(b)) {
                if (IsPlane(b)) std::swap(a, b);
                if (HasAxis(b)) out.push_back(a.d.Dot(b.d) * kScale);   // an axis lies in the plane
                out.push_back(DistanceRow(c, (b.p - a.p).Dot(a.d)));
            } else if (HasAxis(a) || HasAxis(b)) {
                if (HasAxis(b)) std::swap(a, b);
                Push(out, Across(b.p - a.p, a.d));   // a point on a line
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
            if (c.limits.enabled) {
                const double angle = std::acos(std::clamp(a.d.Dot(b.d), -1.0, 1.0));
                out.push_back(OutsideLimits(JointMotion::Slide, c.limits, angle) * kScale);
            } else {
                out.push_back((a.d.Dot(b.d) - std::cos(c.value)) * kScale);
            }
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
        case ConstraintType::Symmetry: {
            // `b` is `a` mirrored in the plane `s` (normal n through point o).
            const Vec3 n = s ? s->d : Vec3{0, 0, 1};
            const Vec3 o = s ? s->p : Vec3{};
            const auto mirror = [&](Vec3 p) { return p - n * (2 * (p - o).Dot(n)); };
            const auto mirrorDir = [&](Vec3 v) { return v - n * (2 * v.Dot(n)); };
            const Vec3 ma = mirror(a.p);
            if (IsPlane(a) && IsPlane(b)) {
                // Mirrored faces: the mirror of one's normal is the other's (flipped: opposed).
                Push(out, b.d - mirrorDir(a.d) * sign, kScale);
                out.push_back((b.p - ma).Dot(b.d));
            } else if (HasAxis(a) && HasAxis(b)) {
                Push(out, b.d.Cross(mirrorDir(a.d)), kScale);
                Push(out, Across(b.p - ma, b.d));
            } else {
                Push(out, b.p - ma);
            }
            break;
        }
        case ConstraintType::Rotational:
            Push(out, facing, kScale);
            Push(out, Across(b.p - a.p, a.d));
            out.push_back((b.p - a.p).Dot(a.d) - c.value);
            break;
        case ConstraintType::Slider:
            Push(out, facing, kScale);
            Push(out, Across(b.p - a.p, a.d));
            Push(out, b.x - a.x, kScale);
            break;
        case ConstraintType::Cylindrical:
            Push(out, facing, kScale);
            Push(out, Across(b.p - a.p, a.d));
            break;
        case ConstraintType::Planar:
            Push(out, facing, kScale);
            out.push_back((b.p - a.p).Dot(a.d) - c.value);
            break;
        case ConstraintType::Ball:
            Push(out, b.p - a.p);
            break;
        case ConstraintType::Rigid:
            Push(out, facing, kScale);
            Push(out, b.p - a.p - a.d * c.value);
            Push(out, b.x - a.x, kScale);
            break;
        case ConstraintType::Gear:
        case ConstraintType::Screw: break;   // not geometric: see TransmissionRow
    }
    // The coordinate of a one-coordinate joint: held when locked, kept within
    // limits. Always the same number of rows (zero when free).
    if (const JointMotion motion = MotionOf(c.type); motion != JointMotion::None) {
        const double q = Coordinate(c.type, a, b);
        const double k = motion == JointMotion::Rotation ? kScale : 1.0;
        const double off = motion == JointMotion::Rotation ? Wrap(q - c.position) : q - c.position;
        out.push_back(c.locked ? off * k : 0.0);
        if (limits) out.push_back(c.limits.enabled ? OutsideLimits(motion, c.limits, q) * k : 0.0);
    }
}

/// How far a transmission is from holding, given its joints' coordinates
/// now (rad or mm), in mm.
double TransmissionRow(const Constraint& t, ConstraintType drivenType, double qDrive, double qDriven) {
    const double sign = t.flipped ? -1.0 : 1.0;
    const double turned = Wrap(qDrive - t.driveAt);   // since it last held
    if (t.type == ConstraintType::Gear) {
        // Both rotate: an angle error, weighed like other angles.
        if (MotionOf(drivenType) != JointMotion::Rotation) return 0.0;
        return Wrap(qDriven - t.drivenAt - sign * t.ratio * turned) * kScale;
    }
    // Screw: the driven slider moves `ratio` mm per turn.
    if (MotionOf(drivenType) != JointMotion::Slide) return 0.0;
    return (qDriven - t.drivenAt) - sign * t.ratio * turned / (2 * kPi);
}

// --- small dense linear algebra --------------------------------------------------

using Matrix = std::vector<std::vector<double>>;

/// Solves A x = b for a symmetric positive definite A (the damped normal
/// equations) by Cholesky. False when A is not positive definite.
bool SolveSpd(const Matrix& a, const std::vector<double>& b, std::vector<double>& x) {
    const std::size_t n = b.size();
    Matrix l(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double s = a[i][j];
            const std::vector<double>& li = l[i];
            const std::vector<double>& lj = l[j];
            for (std::size_t k = 0; k < j; ++k) s -= li[k] * lj[k];
            if (i == j) {
                if (!(s > 0.0)) return false;
                l[i][i] = std::sqrt(s);
            } else {
                l[i][j] = s / l[j][j];
            }
        }
    }
    std::vector<double> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        double s = b[i];
        for (std::size_t k = 0; k < i; ++k) s -= l[i][k] * y[k];
        y[i] = s / l[i][i];
    }
    x.assign(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double s = y[i];
        for (std::size_t k = i + 1; k < n; ++k) s -= l[k][i] * x[k];
        x[i] = s / l[i][i];
    }
    return true;
}

/// A sparse row of the Jacobian: (column, value).
using SparseRow = std::vector<std::pair<std::size_t, double>>;

/// JᵀJ of a group, kept as 6×6 blocks: block (i, j) is non-zero only when a
/// constraint ties components i and j. A large group (a long chain of
/// parts) then costs what its ties cost, not the cube of its size.
struct BlockMatrix {
    using Block = std::array<double, 36>;
    std::size_t blocks = 0;
    std::vector<std::vector<std::pair<std::size_t, Block>>> rows;   ///< per block row: (block column, block)

    explicit BlockMatrix(std::size_t n) : blocks(n), rows(n) {}

    Block& At(std::size_t i, std::size_t j) {
        for (auto& [c, b] : rows[i])
            if (c == j) return b;
        rows[i].push_back({j, Block{}});
        return rows[i].back().second;
    }
};

// 6×6 blocks: row-major, element (r, c) at r * 6 + c.
using Block6 = std::array<double, 36>;

/// Lower Cholesky factor of a symmetric 6×6 block; false when it is not
/// positive definite.
bool Cholesky6(const Block6& a, Block6& l) {
    l.fill(0.0);
    for (std::size_t i = 0; i < 6; ++i)
        for (std::size_t j = 0; j <= i; ++j) {
            double s = a[i * 6 + j];
            for (std::size_t k = 0; k < j; ++k) s -= l[i * 6 + k] * l[j * 6 + k];
            if (i == j) {
                if (!(s > 0.0)) return false;
                l[i * 6 + i] = std::sqrt(s);
            } else {
                l[i * 6 + j] = s / l[j * 6 + j];
            }
        }
    return true;
}

/// v := L⁻¹ v (L lower).
void LowerSolve(const Block6& l, double* v) {
    for (std::size_t i = 0; i < 6; ++i) {
        double s = v[i];
        for (std::size_t k = 0; k < i; ++k) s -= l[i * 6 + k] * v[k];
        v[i] = s / l[i * 6 + i];
    }
}

/// v := L⁻ᵀ v (L lower).
void UpperSolve(const Block6& l, double* v) {
    for (std::size_t i = 6; i-- > 0;) {
        double s = v[i];
        for (std::size_t k = i + 1; k < 6; ++k) s -= l[k * 6 + i] * v[k];
        v[i] = s / l[i * 6 + i];
    }
}

/// Solves (A + damping) x = b exactly by Cholesky on 6×6 blocks, the block
/// with the fewest neighbours eliminated first (minimum degree): a chain or
/// a tree of parts — what assemblies mostly are — factors with little fill,
/// in about linear time. False when the system is not positive definite.
bool SolveBlocks(const BlockMatrix& a, double lambda, const std::vector<double>& b, std::vector<double>& x) {
    const std::size_t nb = a.blocks;
    std::vector<std::map<std::size_t, Block6>> m(nb);   // the working matrix, both triangles
    for (std::size_t i = 0; i < nb; ++i)
        for (const auto& [j, blk] : a.rows[i]) {
            Block6& t = m[i][j];
            for (std::size_t k = 0; k < 36; ++k) t[k] += blk[k];
        }
    for (std::size_t i = 0; i < nb; ++i) {
        Block6& d = m[i][i];
        for (std::size_t r = 0; r < 6; ++r) d[r * 6 + r] += lambda * (1.0 + d[r * 6 + r]);
    }

    std::vector<bool> done(nb, false);
    std::vector<std::size_t> order;
    order.reserve(nb);
    std::vector<Block6> diag(nb);                                          // L_pp
    std::vector<std::vector<std::pair<std::size_t, Block6>>> below(nb);   // (i, L_ip) for p
    for (std::size_t step = 0; step < nb; ++step) {
        // The block with the fewest neighbours left.
        std::size_t p = nb, best = ~std::size_t{0};
        for (std::size_t i = 0; i < nb; ++i) {
            if (done[i]) continue;
            std::size_t degree = 0;
            for (const auto& [j, blk] : m[i]) degree += (j != i && !done[j]) ? std::size_t{1} : std::size_t{0};
            if (degree < best) {
                best = degree;
                p = i;
            }
        }
        Block6& lp = diag[p];
        if (!Cholesky6(m[p][p], lp)) return false;
        // L_ip = A_ip L_pp⁻ᵀ: each row of A_ip solved against L_pp.
        std::vector<std::pair<std::size_t, Block6>>& col = below[p];
        for (auto& [i, blk] : m[p]) {
            if (i == p || done[i]) continue;
            Block6 lip = m[i][p];
            for (std::size_t r = 0; r < 6; ++r) LowerSolve(lp, &lip[r * 6]);   // row r: L_pp z = a_r (z = row of L_ip)
            col.push_back({i, lip});
        }
        // Schur complement: A_ij -= L_ip L_jpᵀ for every pair of neighbours (the fill).
        for (const auto& [i, li] : col)
            for (const auto& [j, lj] : col) {
                Block6& t = m[i][j];
                for (std::size_t r = 0; r < 6; ++r)
                    for (std::size_t c = 0; c < 6; ++c) {
                        double s = 0.0;
                        for (std::size_t k = 0; k < 6; ++k) s += li[r * 6 + k] * lj[c * 6 + k];
                        t[r * 6 + c] -= s;
                    }
            }
        done[p] = true;
        order.push_back(p);
    }
    // L y = b, then Lᵀ x = y, both in the elimination order.
    x = b;
    for (std::size_t p : order) {
        LowerSolve(diag[p], &x[6 * p]);
        for (const auto& [i, lip] : below[p])
            for (std::size_t r = 0; r < 6; ++r) {
                double s = 0.0;
                for (std::size_t k = 0; k < 6; ++k) s += lip[r * 6 + k] * x[6 * p + k];
                x[6 * i + r] -= s;
            }
    }
    for (std::size_t n = order.size(); n-- > 0;) {
        const std::size_t p = order[n];
        for (const auto& [i, lip] : below[p])
            for (std::size_t k = 0; k < 6; ++k) {
                double s = 0.0;
                for (std::size_t r = 0; r < 6; ++r) s += lip[r * 6 + k] * x[6 * i + r];
                x[6 * p + k] -= s;
            }
        UpperSolve(diag[p], &x[6 * p]);
    }
    for (double v : x)
        if (!std::isfinite(v)) return false;
    return true;
}

/// Numerical rank (full pivoting) of the columns `cols` of sparse `rows`.
int Rank(const std::vector<SparseRow>& rows, const std::vector<std::size_t>& cols) {
    if (rows.empty() || cols.empty()) return 0;
    std::vector<std::size_t> where(*std::max_element(cols.begin(), cols.end()) + 1, ~std::size_t{0});
    for (std::size_t c = 0; c < cols.size(); ++c) where[cols[c]] = c;
    Matrix a;
    double biggest = 0.0;
    for (const SparseRow& r : rows) {
        std::vector<double> dense(cols.size(), 0.0);
        bool any = false;
        for (const auto& [col, v] : r)
            if (col < where.size() && where[col] != ~std::size_t{0}) {
                dense[where[col]] = v;
                biggest = std::max(biggest, std::abs(v));
                any = any || v != 0.0;
            }
        if (any) a.push_back(std::move(dense));
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
            if (f == 0.0) continue;
            for (std::size_t c = 0; c < cols.size(); ++c) a[r][c] -= f * a[br][c];
        }
    }
}

// --- the system ---------------------------------------------------------------------

/// One group of free components and the constraints that tie them. The
/// unknowns: six per free component, measured from where it started.
struct System {
    Assembly&                      assembly;
    std::vector<const Constraint*> active;
    std::vector<std::vector<std::size_t>> vars;   ///< per constraint: indices into `free` it depends on
    std::vector<ObjectId>          free;          ///< components with unknowns
    std::vector<Placement>         start;         ///< their placements at the start
    std::vector<double>            weight;        ///< how readily each moves (1; less for the kept one)
    std::unordered_map<ObjectId, std::size_t> index;
    bool                           limits = true;   ///< with the joint-limit rows

    explicit System(Assembly& a) : assembly(a) {}

    std::size_t Unknowns() const { return 6 * free.size(); }

    Placement PoseOf(std::size_t i, const std::vector<double>& y) const {
        const double* v = &y[6 * i];
        const double s = weight[i];
        const Vec3 shift{v[0] * s, v[1] * s, v[2] * s};
        const Vec3 turn{v[3] * s / kScale, v[4] * s / kScale, v[5] * s / kScale};
        return {Rotation::FromVector(turn) * start[i].rotation, start[i].translation + shift};
    }

    Placement PlacementOf(ObjectId id, const std::vector<double>& y) const {
        if (const auto it = index.find(id); it != index.end() && !y.empty()) return PoseOf(it->second, y);
        return assembly.PlacementOf(id);
    }

    std::optional<double> CoordinateOf(const Constraint& joint, const std::vector<double>& y) const {
        if (MotionOf(joint.type) == JointMotion::None) return std::nullopt;
        return Coordinate(joint.type, ToWorld(PlacementOf(joint.first.component, y), joint.first),
                          ToWorld(PlacementOf(joint.second.component, y), joint.second));
    }

    /// Residuals of one constraint, appended to `out`. Empty `y`: at the
    /// components' current places.
    void Rows(const Constraint& c, const std::vector<double>& y, std::vector<double>& out) const {
        if (IsTransmission(c.type)) {
            const Constraint* drive = assembly.FindConstraint(c.drive);
            const Constraint* driven = assembly.FindConstraint(c.driven);
            const std::optional<double> q1 = drive ? CoordinateOf(*drive, y) : std::nullopt;
            const std::optional<double> q2 = driven ? CoordinateOf(*driven, y) : std::nullopt;
            out.push_back(q1 && q2 ? TransmissionRow(c, driven->type, *q1, *q2) : 0.0);
            return;
        }
        const bool symmetry = c.type == ConstraintType::Symmetry;
        const WorldGeometry s = symmetry ? ToWorld(PlacementOf(c.third.component, y), c.third) : WorldGeometry{};
        Residuals(c, ToWorld(PlacementOf(c.first.component, y), c.first), ToWorld(PlacementOf(c.second.component, y), c.second),
                  symmetry ? &s : nullptr, out, limits);
    }

    /// Residuals of all active constraints; `rows` gets where each starts.
    std::vector<double> F(const std::vector<double>& y, std::vector<std::size_t>* rows = nullptr) const {
        std::vector<double> out;
        if (rows) rows->clear();
        for (const Constraint* c : active) {
            if (rows) rows->push_back(out.size());
            Rows(*c, y, out);
        }
        if (rows) rows->push_back(out.size());
        return out;
    }

    /// The Jacobian at `y`, row by row: each constraint is differentiated
    /// only by the unknowns of the components it depends on (a handful), so
    /// the cost grows with the number of constraints, not with constraints
    /// times components.
    std::vector<SparseRow> J(const std::vector<double>& y, const std::vector<double>& f0,
                             const std::vector<std::size_t>& rows) const {
        std::vector<SparseRow> j(f0.size());
        constexpr double h = 1e-6;
        std::vector<double> yh = y;
        std::vector<double> f1;
        for (std::size_t k = 0; k < active.size(); ++k) {
            const std::size_t r0 = rows[k], r1 = rows[k + 1];
            if (r0 == r1) continue;
            for (std::size_t i : vars[k])
                for (std::size_t v = 6 * i; v < 6 * i + 6; ++v) {
                    yh[v] = y[v] + h;
                    f1.clear();
                    Rows(*active[k], yh, f1);
                    yh[v] = y[v];
                    for (std::size_t r = r0; r < r1; ++r) {
                        const double d = (f1[r - r0] - f0[r]) / h;
                        if (d != 0.0) j[r].emplace_back(v, d);
                    }
                }
        }
        return j;
    }
};

double Norm(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v) s += x * x;
    return std::sqrt(s);
}

/// Whether `c` wants the directions of its two sides opposed (true) or the
/// same way (false); nullopt when it has no such rows.
std::optional<bool> WantsOpposed(const Constraint& c, const WorldGeometry& a, const WorldGeometry& b) {
    switch (c.type) {
        case ConstraintType::Mate:
            if (IsPlane(a) && IsPlane(b)) return !c.flipped;
            return std::nullopt;
        case ConstraintType::Insert:
        case ConstraintType::Rotational:
        case ConstraintType::Slider:
        case ConstraintType::Cylindrical:
        case ConstraintType::Planar:
        case ConstraintType::Rigid: return !c.flipped;
        default: return std::nullopt;
    }
}

/// A part that stands exactly the wrong way round for a constraint (its
/// direction opposed where the constraint wants the same, or the other way)
/// is a dead point for the solver: no small turn makes it better, it cannot
/// start. Such a part is turned over first — half a turn about an axis
/// square to its direction, through its point. True when a part was turned.
bool TurnOverBackwards(Assembly& assembly) {
    bool turned = false;
    for (const Constraint& c : assembly.constraints) {
        if (c.suppressed || IsTransmission(c.type) || c.type == ConstraintType::Symmetry) continue;
        if (!assembly.Exists(c.first.component) || !assembly.Exists(c.second.component)) continue;
        if (c.first.component == c.second.component) continue;
        const WorldGeometry wa = ToWorld(assembly.PlacementOf(c.first.component), c.first);
        const WorldGeometry wb = ToWorld(assembly.PlacementOf(c.second.component), c.second);
        const std::optional<bool> opposed = WantsOpposed(c, wa, wb);
        if (!opposed) continue;
        const double along = wa.d.Dot(wb.d);
        if (!(*opposed ? along > 1 - 1e-6 : along < -1 + 1e-6)) continue;
        Component* first = assembly.FindComponent(c.first.component);
        Component* second = assembly.FindComponent(c.second.component);
        Component* part = first && !first->grounded ? first : second && !second->grounded ? second : nullptr;
        if (!part) continue;
        const WorldGeometry& g = part == first ? wa : wb;
        Vec3 axis = g.x - g.d * g.x.Dot(g.d);
        if (axis.Length() < 1e-6) {
            const Vec3 e = std::abs(g.d.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            axis = e - g.d * e.Dot(g.d);
        }
        const Rotation half = Rotation::AxisAngle(axis.Normalized(), kPi);
        part->placement = {(half * part->placement.rotation).Orthonormalized(), half * (part->placement.translation - g.p) + g.p};
        turned = true;
    }
    return turned;
}

bool Usable(const Assembly& a, const Constraint& c) {
    if (c.suppressed) return false;
    if (IsTransmission(c.type)) {
        const Constraint* d = a.FindConstraint(c.drive);
        const Constraint* n = a.FindConstraint(c.driven);
        return d && n && IsJoint(d->type) && IsJoint(n->type) && !d->suppressed && !n->suppressed;
    }
    return a.Exists(c.first.component) && a.Exists(c.second.component) &&
           (c.type != ConstraintType::Symmetry || a.Exists(c.third.component));
}

/// The groups: constraints that share a free component belong together.
/// Constraints with no free component at all come in a group of their own
/// (nothing to move, only to check).
std::vector<System> MakeSystems(Assembly& a, ObjectId keep) {
    std::vector<const Constraint*> usable;
    for (const Constraint& c : a.constraints)
        if (Usable(a, c)) usable.push_back(&c);

    // Union-find over free components.
    std::unordered_map<ObjectId, ObjectId> parent;
    const auto find = [&](ObjectId x) {
        ObjectId r = x;
        while (parent[r] != r) r = parent[r];
        while (parent[x] != r) {
            const ObjectId next = parent[x];
            parent[x] = r;
            x = next;
        }
        return r;
    };
    std::vector<std::vector<ObjectId>> freeOf(usable.size());
    for (std::size_t k = 0; k < usable.size(); ++k) {
        for (ObjectId id : a.ComponentsOf(*usable[k]))
            if (!a.IsFixed(id)) freeOf[k].push_back(id);
        for (ObjectId id : freeOf[k])
            if (!parent.contains(id)) parent[id] = id;
        for (std::size_t i = 1; i < freeOf[k].size(); ++i) {
            const ObjectId r0 = find(freeOf[k][0]), r1 = find(freeOf[k][i]);
            if (r0 != r1) parent[r1] = r0;
        }
    }
    std::vector<System> out;
    std::unordered_map<ObjectId, std::size_t> groupOf;   // root -> system
    std::optional<std::size_t> fixedGroup;
    for (std::size_t k = 0; k < usable.size(); ++k) {
        std::size_t g;
        if (freeOf[k].empty()) {
            if (!fixedGroup) {
                fixedGroup = out.size();
                out.emplace_back(a);
            }
            g = *fixedGroup;
        } else {
            const ObjectId root = find(freeOf[k][0]);
            const auto it = groupOf.find(root);
            if (it == groupOf.end()) {
                g = out.size();
                groupOf[root] = g;
                out.emplace_back(a);
            } else {
                g = it->second;
            }
        }
        System& s = out[g];
        std::vector<std::size_t> vars;
        for (ObjectId id : freeOf[k]) {
            auto it = s.index.find(id);
            if (it == s.index.end()) {
                it = s.index.emplace(id, s.free.size()).first;
                s.free.push_back(id);
                s.start.push_back(a.FindComponent(id)->placement);
                s.weight.push_back(id == keep ? 0.05 : 1.0);
            }
            vars.push_back(it->second);
        }
        s.active.push_back(usable[k]);
        s.vars.push_back(std::move(vars));
    }
    return out;
}

/// Groups up to this many unknowns are solved with a dense Cholesky; larger
/// ones with a sparse one on 6×6 blocks.
constexpr std::size_t kDenseLimit = 120;

/// Damped least squares on one group; moves its components. Returns the
/// error left.
double SolveSystem(System& s, const SolveOptions& options, int& iterations) {
    std::vector<double> y(s.Unknowns(), 0.0);
    std::vector<std::size_t> rows;
    std::vector<double> f = s.F(y, &rows);
    double err = Norm(f);
    double lambda = 1e-3;
    const std::size_t n = y.size();
    const bool dense = n <= kDenseLimit;
    int own = 0, stalled = 0;
    while (n > 0 && err > options.tolerance && own < options.maxIterations) {
        ++own;
        ++iterations;
        const std::vector<SparseRow> j = s.J(y, f, rows);
        // Normal equations, damped: (JᵀJ + λI) dy = -Jᵀf. With λ small the step
        // is the shortest one that removes the error.
        Matrix jtj;
        BlockMatrix blocks(dense ? 0 : s.free.size());
        if (dense) jtj.assign(n, std::vector<double>(n, 0.0));
        std::vector<double> jtf(n, 0.0);
        for (std::size_t r = 0; r < f.size(); ++r)
            for (const auto& [p, vp] : j[r]) {
                jtf[p] -= vp * f[r];
                if (dense) {
                    for (const auto& [q, vq] : j[r]) jtj[p][q] += vp * vq;
                } else {
                    for (const auto& [q, vq] : j[r]) blocks.At(p / 6, q / 6)[(p % 6) * 6 + q % 6] += vp * vq;
                }
            }
        bool improved = false;
        const double before = err;
        for (int tries = 0; tries < 12 && !improved; ++tries) {
            std::vector<double> dy;
            bool solved;
            if (dense) {
                Matrix damped = jtj;
                for (std::size_t p = 0; p < n; ++p) damped[p][p] += lambda * (1.0 + jtj[p][p]);
                solved = SolveSpd(damped, jtf, dy);
            } else {
                solved = SolveBlocks(blocks, lambda, jtf, dy);
            }
            if (!solved) {
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
        // Creeping towards a minimum that is not a solution (constraints that
        // contradict): a few steps that gain next to nothing end it, rather
        // than all the iterations.
        stalled = before - err < 1e-6 * before ? stalled + 1 : 0;
        if (stalled >= 4) break;
    }
    for (std::size_t i = 0; i < s.free.size() && own > 0; ++i) {
        Component* c = s.assembly.FindComponent(s.free[i]);
        const Placement p = s.PoseOf(i, y);
        c->placement = {p.rotation.Orthonormalized(), p.translation};
    }
    return err;
}

/// States of the constraints of one group at the current placements.
void AnalyzeSystem(System& s) {
    s.limits = false;   // a joint at its limit is not "redundant" or "failed" for it
    for (std::size_t i = 0; i < s.free.size(); ++i) s.start[i] = s.assembly.FindComponent(s.free[i])->placement;
    const std::vector<double> y0(s.Unknowns(), 0.0);
    std::vector<std::size_t> rows;
    const std::vector<double> f0 = s.F(y0, &rows);
    const std::vector<SparseRow> j = y0.empty() ? std::vector<SparseRow>(f0.size()) : s.J(y0, f0, rows);

    // Redundant: it holds and adds nothing to the constraints before it. Each
    // row is reduced against the rows before it kept in echelon form (sparse
    // Gaussian elimination, each kept row led by its first column): a
    // constraint all of whose rows vanish says nothing new. Sparse, so a
    // long chain of parts costs about its length, not its cube.
    double biggest = 0.0;
    for (const SparseRow& r : j)
        for (const auto& [col, v] : r) biggest = std::max(biggest, std::abs(v));
    const double tol = std::max(biggest, 1e-12) * 1e-7;
    std::unordered_map<std::size_t, std::map<std::size_t, double>> echelon;   // leading column -> row (lead = 1)
    for (std::size_t k = 0; k < s.active.size(); ++k) {
        auto* c = const_cast<Constraint*>(s.active[k]);
        std::vector<double> own(f0.begin() + static_cast<std::ptrdiff_t>(rows[k]),
                                f0.begin() + static_cast<std::ptrdiff_t>(rows[k + 1]));
        c->error = Norm(own);
        c->state = c->error > kFailed ? ConstraintState::Failed : ConstraintState::Satisfied;
        if (y0.empty()) continue;
        bool adds = false;
        for (std::size_t r = rows[k]; r < rows[k + 1]; ++r) {
            std::map<std::size_t, double> v;
            for (const auto& [col, val] : j[r])
                if (std::abs(val) > tol) v[col] += val;
            // Reduce from the left: every leading column met is cleared.
            for (auto it = v.begin(); it != v.end();) {
                if (std::abs(it->second) <= tol) {
                    it = v.erase(it);
                    continue;
                }
                const auto lead = echelon.find(it->first);
                if (lead == echelon.end()) {
                    ++it;
                    continue;
                }
                const double factor = it->second;
                const std::size_t col = it->first;
                for (const auto& [q, b] : lead->second) v[q] -= factor * b;   // only columns >= col
                it = v.find(col);
                it = v.erase(it);   // exactly zero now
            }
            if (v.empty()) continue;
            // Kept: led by its first column, scaled so the lead is 1.
            const std::size_t leadCol = v.begin()->first;
            const double leadValue = v.begin()->second;
            for (auto& [q, b] : v) b /= leadValue;
            echelon.emplace(leadCol, std::move(v));
            adds = true;
        }
        if (!adds && c->state == ConstraintState::Satisfied) c->state = ConstraintState::Redundant;
    }
}

void ResetStates(Assembly& a) {
    for (Constraint& c : a.constraints) {
        c.state = c.suppressed ? ConstraintState::Unknown : ConstraintState::Satisfied;
        c.error = 0.0;
    }
}

} // namespace

double ConstraintError(const Assembly& assembly, const Constraint& c) {
    if (!Usable(assembly, c)) return 0.0;
    const System s(const_cast<Assembly&>(assembly));
    std::vector<double> out;
    s.Rows(c, {}, out);
    return Norm(out);
}

Vec3 JointFrameX(const GeometryRef& g) {
    const Vec3 d = g.direction.Length() > 0.5 ? g.direction.Normalized() : Vec3{0, 0, 1};
    Vec3 x = g.reference - d * g.reference.Dot(d);
    if (x.Length() < 1e-6) {
        const Vec3 e = std::abs(d.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        x = e - d * e.Dot(d);
    }
    return x.Normalized();
}

std::optional<double> JointPosition(const Assembly& assembly, const Constraint& c) {
    if (!assembly.Exists(c.first.component) || !assembly.Exists(c.second.component) || MotionOf(c.type) == JointMotion::None)
        return std::nullopt;
    return Coordinate(c.type, ToWorld(assembly.PlacementOf(c.first.component), c.first),
                      ToWorld(assembly.PlacementOf(c.second.component), c.second));
}

std::optional<double> JointCoordinate(const Assembly& assembly, ObjectId joint) {
    const Constraint* c = assembly.FindConstraint(joint);
    return c ? JointPosition(assembly, *c) : std::nullopt;
}

void FitTransmission(const Assembly& assembly, Constraint& t) {
    if (const auto q = JointCoordinate(assembly, t.drive)) t.driveAt = *q;
    if (const auto q = JointCoordinate(assembly, t.driven)) t.drivenAt = *q;
}

void FitJoint(const Assembly& assembly, Constraint& c) {
    if (!assembly.Exists(c.first.component) || !assembly.Exists(c.second.component)) return;
    const Placement pa = assembly.PlacementOf(c.first.component), pb = assembly.PlacementOf(c.second.component);
    const WorldGeometry wa = ToWorld(pa, c.first), wb = ToWorld(pb, c.second);
    c.flipped = wa.d.Dot(wb.d) > 0;
    // The first frame's X, square to the second's Z, in the second part's coordinates.
    const Vec3 x = wa.x - wb.d * wa.x.Dot(wb.d);
    if (x.Length() > 1e-9) c.second.reference = pb.rotation.Transposed() * x.Normalized();
    if (MotionOf(c.type) == JointMotion::Slide) c.position = (wb.p - wa.p).Dot(wa.d);
    else if (MotionOf(c.type) == JointMotion::Rotation) c.position = 0.0;
}

SolveResult DriveJoint(Assembly& assembly, ObjectId id, double position, SolveOptions options) {
    Constraint* c = assembly.FindConstraint(id);
    if (!c || MotionOf(c->type) == JointMotion::None) return {};
    const JointMotion motion = MotionOf(c->type);
    if (c->limits.enabled) {
        if (motion == JointMotion::Slide) {
            position = std::clamp(position, c->limits.lower, c->limits.upper);
        } else {
            const double out = OutsideLimits(motion, c->limits, position);
            if (out != 0.0) position -= out;   // to the nearer end
        }
    }
    if (motion == JointMotion::Rotation) position = Wrap(position);
    const bool   wasLocked = c->locked;
    const double was = c->position;
    if (!options.keep) options.keep = c->first.component;
    // A transmission ties changes of less than half a turn at a time (angles
    // wrap): a big turn goes in steps of an eighth, each solved from the last.
    const std::optional<double> from = JointPosition(assembly, *c);
    double sweep = 0.0;
    int steps = 1;
    if (from && motion == JointMotion::Rotation) {
        sweep = Wrap(position - *from);
        steps = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 4))));
    }
    SolveResult r;
    for (int i = 1; i <= steps; ++i) {
        c = assembly.FindConstraint(id);
        c->locked = true;
        c->position = steps == 1 ? position : Wrap(*from + sweep * i / steps);
        SolveOptions o = options;
        o.analyze = options.analyze && i == steps;
        r = Solve(assembly, o);
    }
    c = assembly.FindConstraint(id);
    if (!wasLocked) {
        c->locked   = false;
        c->position = was;
    }
    assembly.Touch();
    return r;
}

SolveResult Solve(Assembly& a, const SolveOptions& options) {
    SolveResult result;
    ResetStates(a);
    result.moved = TurnOverBackwards(a);
    std::vector<System> systems = MakeSystems(a, options.keep);
    double total = 0.0;
    for (System& s : systems) {
        if (s.free.empty()) continue;
        std::vector<Placement> before;
        for (ObjectId id : s.free) before.push_back(a.FindComponent(id)->placement);
        const double err = SolveSystem(s, options, result.iterations);
        total += err * err;
        for (std::size_t i = 0; i < s.free.size(); ++i) {
            const Placement& now = a.FindComponent(s.free[i])->placement;
            const Placement& was = before[i];
            if ((now.translation - was.translation).Length() > 1e-12 ||
                (now.rotation.col[0] - was.rotation.col[0]).Length() > 1e-12 ||
                (now.rotation.col[2] - was.rotation.col[2]).Length() > 1e-12)
                result.moved = true;
        }
    }
    result.error = std::sqrt(total);
    result.converged = result.error <= options.tolerance * 10;

    // Transmissions that hold start again from here: they tie changes, and a
    // joint may turn on past half a turn over many solves.
    const System now(a);
    for (Constraint& c : a.constraints) {
        if (!IsTransmission(c.type) || !Usable(a, c)) continue;
        std::vector<double> row;
        now.Rows(c, {}, row);
        if (!row.empty() && std::abs(row[0]) <= kFailed) FitTransmission(a, c);
    }

    if (options.analyze)
        for (System& s : MakeSystems(a, {})) AnalyzeSystem(s);
    return result;
}

void Analyze(Assembly& a) {
    ResetStates(a);
    for (System& s : MakeSystems(a, {})) AnalyzeSystem(s);
}

std::unordered_map<ObjectId, Freedom> AnalyzeFreedom(const Assembly& a) {
    std::unordered_map<ObjectId, Freedom> out;
    auto& mutableAssembly = const_cast<Assembly&>(a);   // the systems only read it here
    for (const Component& c : a.components) out[c.id] = c.grounded ? Freedom{0, 0} : Freedom{3, 3};
    for (System& s : MakeSystems(mutableAssembly, {})) {
        if (s.free.empty()) continue;
        s.limits = false;
        const std::vector<double> y(s.Unknowns(), 0.0);
        std::vector<std::size_t> rows;
        const std::vector<double> f = s.F(y, &rows);
        const std::vector<SparseRow> j = s.J(y, f, rows);
        // Freedom of each one with the others held where they are: only the
        // rows of the constraints that tie it count.
        for (std::size_t i = 0; i < s.free.size(); ++i) {
            std::vector<SparseRow> mine;
            for (std::size_t k = 0; k < s.active.size(); ++k)
                if (std::find(s.vars[k].begin(), s.vars[k].end(), i) != s.vars[k].end())
                    for (std::size_t r = rows[k]; r < rows[k + 1]; ++r) mine.push_back(j[r]);
            const std::vector<std::size_t> shift{6 * i, 6 * i + 1, 6 * i + 2};
            const std::vector<std::size_t> all{6 * i, 6 * i + 1, 6 * i + 2, 6 * i + 3, 6 * i + 4, 6 * i + 5};
            const int shiftRank = Rank(mine, shift);
            const int rank = Rank(mine, all);
            out[s.free[i]] = {3 - shiftRank, 3 - (rank - shiftRank)};
        }
    }
    return out;
}

} // namespace bz::assembly
