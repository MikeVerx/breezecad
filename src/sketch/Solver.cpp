#include "sketch/Solver.h"

#include "sketch/EquationSystem.h"

#include <algorithm>
#include <cmath>

namespace bz::sketch {
namespace {

using detail::Equation;
using detail::EquationSystem;

constexpr double kTolerance = 1e-9;   // mm for distances, rad / sin for angles

/// Dense row-major matrix, just enough for small sketch systems.
struct Matrix {
    int                 rows = 0;
    int                 cols = 0;
    std::vector<double> data;

    Matrix(int r, int c) : rows(r), cols(c), data(static_cast<std::size_t>(r) * static_cast<std::size_t>(c), 0.0) {}
    double& At(int r, int c) { return data[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]; }
    double  At(int r, int c) const { return data[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]; }
};

/// Maps unknowns to Jacobian columns; fixed unknowns get -1.
struct Columns {
    std::vector<int> ofUnknown;
    int              count = 0;

    explicit Columns(const EquationSystem& sys) : ofUnknown(sys.x.size(), -1) {
        for (std::size_t i = 0; i < sys.x.size(); ++i)
            if (sys.isFree[i]) ofUnknown[i] = count++;
    }
};

std::vector<double> Residuals(const EquationSystem& sys, const std::vector<double>& xs) {
    std::vector<double> f(sys.equations.size());
    for (std::size_t i = 0; i < f.size(); ++i) f[i] = sys.Residual(sys.equations[i], xs);
    return f;
}

double MaxAbs(const std::vector<double>& v) {
    double m = 0.0;
    for (double e : v) m = std::max(m, std::abs(e));
    return m;
}

double SumSquares(const std::vector<double>& v) {
    double s = 0.0;
    for (double e : v) s += e * e;
    return s;
}

/// Solves the symmetric positive definite system A y = b in place (Cholesky).
/// Returns false when A is not positive definite.
bool SolveSpd(Matrix& A, std::vector<double>& b) {
    const int n = A.rows;
    for (int j = 0; j < n; ++j) {
        double d = A.At(j, j);
        for (int k = 0; k < j; ++k) d -= A.At(j, k) * A.At(j, k);
        if (d <= 0.0) return false;
        const double ljj = std::sqrt(d);
        A.At(j, j) = ljj;
        for (int i = j + 1; i < n; ++i) {
            double s = A.At(i, j);
            for (int k = 0; k < j; ++k) s -= A.At(i, k) * A.At(j, k);
            A.At(i, j) = s / ljj;
        }
    }
    for (int i = 0; i < n; ++i) {   // L z = b
        double s = b[static_cast<std::size_t>(i)];
        for (int k = 0; k < i; ++k) s -= A.At(i, k) * b[static_cast<std::size_t>(k)];
        b[static_cast<std::size_t>(i)] = s / A.At(i, i);
    }
    for (int i = n - 1; i >= 0; --i) {   // L^T y = z
        double s = b[static_cast<std::size_t>(i)];
        for (int k = i + 1; k < n; ++k) s -= A.At(k, i) * b[static_cast<std::size_t>(k)];
        b[static_cast<std::size_t>(i)] = s / A.At(i, i);
    }
    return true;
}

/// One independent part of the system: some equations and the free
/// unknowns only they share (unknown index -> its column in this part).
struct Block {
    std::vector<int> equations;
    std::vector<int> unknowns;   ///< free unknown indices, column k = unknowns[k]
};

/// Splits the system into parts that share no free unknown. A sketch of
/// separate shapes is many small systems, not one big one: the dense
/// algebra below grows as the cube of the size, so 100 unconnected
/// rectangles took 0.25 s a frame to drag (2 s for 200), and the shapes
/// already in place are not touched at all. Equations with no free unknown
/// at all go to `fixedOnly`.
std::vector<Block> Blocks(const EquationSystem& sys, const Columns& cols, std::vector<int>& fixedOnly) {
    std::vector<int> parent(static_cast<std::size_t>(cols.count));
    for (int i = 0; i < cols.count; ++i) parent[static_cast<std::size_t>(i)] = i;
    const auto find = [&](int c) {
        while (parent[static_cast<std::size_t>(c)] != c) {
            parent[static_cast<std::size_t>(c)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(c)])];
            c = parent[static_cast<std::size_t>(c)];
        }
        return c;
    };
    std::vector<int> firstCol(sys.equations.size(), -1);
    for (std::size_t e = 0; e < sys.equations.size(); ++e)
        for (const int u : sys.equations[e].Unknowns()) {
            const int col = cols.ofUnknown[static_cast<std::size_t>(u)];
            if (col < 0) continue;
            if (firstCol[e] < 0) firstCol[e] = col;
            else parent[static_cast<std::size_t>(find(col))] = find(firstCol[e]);
        }
    std::vector<int> blockOfRoot(static_cast<std::size_t>(cols.count), -1);
    std::vector<Block> blocks;
    const auto blockOf = [&](int col) -> Block& {
        int& b = blockOfRoot[static_cast<std::size_t>(find(col))];
        if (b < 0) {
            b = static_cast<int>(blocks.size());
            blocks.emplace_back();
        }
        return blocks[static_cast<std::size_t>(b)];
    };
    for (std::size_t e = 0; e < sys.equations.size(); ++e) {
        if (firstCol[e] < 0) fixedOnly.push_back(static_cast<int>(e));
        else blockOf(firstCol[e]).equations.push_back(static_cast<int>(e));
    }
    for (std::size_t u = 0; u < sys.x.size(); ++u) {
        const int col = cols.ofUnknown[u];
        if (col >= 0 && blockOfRoot[static_cast<std::size_t>(find(col))] >= 0) blockOf(col).unknowns.push_back(static_cast<int>(u));
    }
    return blocks;
}

std::vector<double> Residuals(const EquationSystem& sys, const std::vector<int>& equations, const std::vector<double>& xs) {
    std::vector<double> f(equations.size());
    for (std::size_t i = 0; i < f.size(); ++i) f[i] = sys.Residual(sys.equations[static_cast<std::size_t>(equations[i])], xs);
    return f;
}

/// Jacobian of a block's equations over its unknowns (`colOf` maps each
/// to its column, -1 elsewhere), by central differences. Each equation only
/// touches a handful of unknowns, so this is cheap.
Matrix BlockJacobian(const EquationSystem& sys, const Block& block, const std::vector<int>& colOf) {
    const int m = static_cast<int>(block.equations.size()), n = static_cast<int>(block.unknowns.size());
    Matrix J(m, n);
    std::vector<double> xs = sys.x;
    for (int row = 0; row < m; ++row) {
        const Equation& eq = sys.equations[static_cast<std::size_t>(block.equations[static_cast<std::size_t>(row)])];
        for (const int u : eq.Unknowns()) {
            const int col = colOf[static_cast<std::size_t>(u)];
            if (col < 0) continue;
            double& xu = xs[static_cast<std::size_t>(u)];
            const double saved = xu;
            const double h = 1e-7 * std::max(1.0, std::abs(saved));
            xu = saved + h;
            const double fPlus = sys.Residual(eq, xs);
            xu = saved - h;
            const double fMinus = sys.Residual(eq, xs);
            xu = saved;
            J.At(row, col) = (fPlus - fMinus) / (2.0 * h);
        }
    }
    return J;
}

/// Minimum-norm damped Gauss-Newton on one block: dx = -J^T (J J^T + mu I)^-1 F.
/// The minimum-norm step moves under-defined geometry as little as possible.
bool IterateBlock(EquationSystem& sys, const Block& block, std::vector<int>& colOf, int maxIterations) {
    std::vector<double> f = Residuals(sys, block.equations, sys.x);
    if (MaxAbs(f) < kTolerance) return true;
    const int m = static_cast<int>(block.equations.size());
    const int n = static_cast<int>(block.unknowns.size());
    for (int k = 0; k < n; ++k) colOf[static_cast<std::size_t>(block.unknowns[static_cast<std::size_t>(k)])] = k;

    bool done = false;
    double mu = 1e-10;
    for (int iter = 0; iter < maxIterations && !done; ++iter) {
        const Matrix J = BlockJacobian(sys, block, colOf);

        Matrix A(m, m);
        for (int i = 0; i < m; ++i)
            for (int j = 0; j <= i; ++j) {
                double s = 0.0;
                for (int k = 0; k < n; ++k) s += J.At(i, k) * J.At(j, k);
                A.At(i, j) = s;
                A.At(j, i) = s;
            }
        for (int i = 0; i < m; ++i) A.At(i, i) += mu * (1.0 + A.At(i, i));

        std::vector<double> y = f;
        if (!SolveSpd(A, y)) {
            mu *= 100.0;
            continue;
        }
        std::vector<double> dx(static_cast<std::size_t>(n), 0.0);
        for (int k = 0; k < n; ++k)
            for (int i = 0; i < m; ++i) dx[static_cast<std::size_t>(k)] -= J.At(i, k) * y[static_cast<std::size_t>(i)];

        // Backtracking: accept the first step length that reduces the error.
        const double before = SumSquares(f);
        bool accepted = false;
        for (double step = 1.0; step > 1e-4; step *= 0.5) {
            std::vector<double> trial = sys.x;
            for (int k = 0; k < n; ++k) trial[static_cast<std::size_t>(block.unknowns[static_cast<std::size_t>(k)])] += step * dx[static_cast<std::size_t>(k)];
            std::vector<double> fTrial = Residuals(sys, block.equations, trial);
            if (SumSquares(fTrial) < before) {
                sys.x = std::move(trial);
                f = std::move(fTrial);
                accepted = true;
                break;
            }
        }
        if (MaxAbs(f) < kTolerance) {
            done = true;
            break;
        }
        mu = accepted ? std::max(mu * 0.1, 1e-12) : mu * 100.0;
        if (mu > 1e6) break;
    }
    for (const int u : block.unknowns) colOf[static_cast<std::size_t>(u)] = -1;
    return done || MaxAbs(f) < kTolerance;
}

/// Solves every independent block of the system on its own.
bool Iterate(EquationSystem& sys, int maxIterations) {
    const Columns cols(sys);
    std::vector<int> fixedOnly;
    const std::vector<Block> blocks = Blocks(sys, cols, fixedOnly);
    // Equations between fixed unknowns only: nothing can make them hold.
    if (MaxAbs(Residuals(sys, fixedOnly, sys.x)) >= kTolerance) return false;
    std::vector<int> colOf(sys.x.size(), -1);
    bool ok = true;
    for (const Block& block : blocks) ok = IterateBlock(sys, block, colOf, maxIterations) && ok;
    return ok;
}

/// Gauss-Jordan elimination. Returns the rank; `determined[c]` is true for
/// columns whose value is fixed by the rows (pivot columns whose row has
/// no entries in non-pivot columns).
int Reduce(Matrix J, std::vector<bool>& determined) {
    double scale = 0.0;
    for (double v : J.data) scale = std::max(scale, std::abs(v));
    const double eps = 1e-8 * std::max(1.0, scale);

    std::vector<int> pivotRowOfCol(static_cast<std::size_t>(J.cols), -1);
    int rank = 0;
    for (int col = 0; col < J.cols && rank < J.rows; ++col) {
        int best = rank;
        for (int r = rank + 1; r < J.rows; ++r)
            if (std::abs(J.At(r, col)) > std::abs(J.At(best, col))) best = r;
        if (std::abs(J.At(best, col)) <= eps) continue;
        for (int c = 0; c < J.cols; ++c) std::swap(J.At(best, c), J.At(rank, c));
        const double pivot = J.At(rank, col);
        for (int c = 0; c < J.cols; ++c) J.At(rank, c) /= pivot;
        for (int r = 0; r < J.rows; ++r) {
            if (r == rank) continue;
            const double factor = J.At(r, col);
            if (factor == 0.0) continue;
            for (int c = 0; c < J.cols; ++c) J.At(r, c) -= factor * J.At(rank, c);
        }
        pivotRowOfCol[static_cast<std::size_t>(col)] = rank;
        ++rank;
    }

    determined.assign(static_cast<std::size_t>(J.cols), false);
    for (int col = 0; col < J.cols; ++col) {
        const int row = pivotRowOfCol[static_cast<std::size_t>(col)];
        if (row < 0) continue;
        bool alone = true;
        for (int c = 0; c < J.cols && alone; ++c)
            if (pivotRowOfCol[static_cast<std::size_t>(c)] < 0 && std::abs(J.At(row, c)) > eps) alone = false;
        determined[static_cast<std::size_t>(col)] = alone;
    }
    return rank;
}

/// Degrees of freedom of a system at its current values, block by block
/// (the rank of a block-diagonal Jacobian is the sum of the blocks' ranks).
int DegreesOfFreedom(const EquationSystem& sys, std::vector<bool>* determinedUnknowns = nullptr) {
    const Columns cols(sys);
    std::vector<int> fixedOnly;
    const std::vector<Block> blocks = Blocks(sys, cols, fixedOnly);
    if (determinedUnknowns) {
        // Fixed unknowns are determined; free ones only if their block says so.
        determinedUnknowns->assign(sys.x.size(), true);
        for (std::size_t u = 0; u < sys.x.size(); ++u)
            if (cols.ofUnknown[u] >= 0) (*determinedUnknowns)[u] = false;
    }
    std::vector<int> colOf(sys.x.size(), -1);
    int rank = 0;
    for (const Block& block : blocks) {
        const int n = static_cast<int>(block.unknowns.size());
        for (int k = 0; k < n; ++k) colOf[static_cast<std::size_t>(block.unknowns[static_cast<std::size_t>(k)])] = k;
        const Matrix J = BlockJacobian(sys, block, colOf);
        std::vector<bool> determinedCols;
        rank += Reduce(J, determinedCols);
        for (int k = 0; k < n; ++k) {
            const auto u = static_cast<std::size_t>(block.unknowns[static_cast<std::size_t>(k)]);
            if (determinedUnknowns) (*determinedUnknowns)[u] = determinedCols[static_cast<std::size_t>(k)];
            colOf[u] = -1;
        }
    }
    return cols.count - rank;
}

void FillAnalysis(const Sketch& sketch, const EquationSystem& sys, SolveResult& result) {
    std::vector<bool> determined;
    result.degreesOfFreedom = DegreesOfFreedom(sys, &determined);

    const auto mark = [&](ObjectId id) {
        const std::vector<int> unknowns = sys.UnknownsOf(sketch, id);
        const bool all = std::all_of(unknowns.begin(), unknowns.end(),
                                     [&](int u) { return determined[static_cast<std::size_t>(u)]; });
        if (all) result.fullyDefined.insert(id);
    };
    for (const Point& p : sketch.points) mark(p.id);
    for (const Line& l : sketch.lines) mark(l.id);
    for (const Circle& c : sketch.circles) mark(c.id);
    for (const Arc& a : sketch.arcs) mark(a.id);
    for (const Spline& s : sketch.splines) mark(s.id);
    for (const Text& t : sketch.texts) mark(t.id);
    for (const Ellipse& e : sketch.ellipses) mark(e.id);
}

void CollectFailing(const EquationSystem& sys, SolveResult& result) {
    for (const Equation& eq : sys.equations) {
        if (!eq.source || std::abs(sys.Residual(eq, sys.x)) < kTolerance * 1e3) continue;
        if (std::find(result.failing.begin(), result.failing.end(), eq.source) == result.failing.end())
            result.failing.push_back(eq.source);
    }
}

} // namespace

SolveResult Solve(Sketch& sketch, const SolveOptions& options) {
    SolveResult result;

    EquationSystem sys(sketch);
    for (const Drag& d : options.drags) sys.Pin(d.point, d.target);
    bool ok = Iterate(sys, options.maxIterations);

    if (!ok && options.drags.size() > 1) {
        // The whole group cannot move there; try moving just the grabbed point.
        sys = EquationSystem(sketch);
        sys.Pin(options.drags.front().point, options.drags.front().target);
        ok = Iterate(sys, options.maxIterations);
    }
    if (!ok && !options.drags.empty()) {
        // The dragged point cannot go there; keep the sketch consistent instead.
        sys = EquationSystem(sketch);
        ok = Iterate(sys, options.maxIterations);
    }

    if (!ok) {
        CollectFailing(sys, result);
        return result;   // sketch untouched
    }

    sys.Apply(sketch);
    result.converged = true;
    FillAnalysis(sketch, EquationSystem(sketch), result);
    return result;
}

SolveResult Analyze(const Sketch& sketch) {
    SolveResult result;
    const EquationSystem sys(sketch);
    result.converged = MaxAbs(Residuals(sys, sys.x)) < kTolerance * 1e3;
    FillAnalysis(sketch, sys, result);
    if (!result.converged) CollectFailing(sys, result);
    return result;
}

bool IsRedundant(const Sketch& sketch, const Constraint& candidate) {
    const EquationSystem without(sketch);
    const EquationSystem with(sketch, {}, &candidate);
    return DegreesOfFreedom(with) >= DegreesOfFreedom(without);
}

} // namespace bz::sketch
