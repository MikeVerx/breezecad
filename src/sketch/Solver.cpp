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

/// Jacobian of all equations over free unknowns, by central differences.
/// Each equation only touches a handful of unknowns, so this is cheap.
Matrix Jacobian(const EquationSystem& sys, const Columns& cols, std::vector<double> xs) {
    Matrix J(static_cast<int>(sys.equations.size()), cols.count);
    for (std::size_t row = 0; row < sys.equations.size(); ++row) {
        const Equation& eq = sys.equations[row];
        for (int slot = 0; slot < eq.count; ++slot) {
            const int u = eq.v[static_cast<std::size_t>(slot)];
            const int col = cols.ofUnknown[static_cast<std::size_t>(u)];
            if (col < 0) continue;
            bool seen = false;   // the same unknown may appear twice in one equation
            for (int prev = 0; prev < slot; ++prev) seen = seen || eq.v[static_cast<std::size_t>(prev)] == u;
            if (seen) continue;

            double& xu = xs[static_cast<std::size_t>(u)];
            const double saved = xu;
            const double h = 1e-7 * std::max(1.0, std::abs(saved));
            xu = saved + h;
            const double fPlus = sys.Residual(eq, xs);
            xu = saved - h;
            const double fMinus = sys.Residual(eq, xs);
            xu = saved;
            J.At(static_cast<int>(row), col) = (fPlus - fMinus) / (2.0 * h);
        }
    }
    return J;
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

/// Minimum-norm damped Gauss-Newton: dx = -J^T (J J^T + mu I)^-1 F.
/// The minimum-norm step moves under-defined geometry as little as possible.
bool Iterate(EquationSystem& sys, int maxIterations) {
    const Columns cols(sys);
    std::vector<double> f = Residuals(sys, sys.x);
    if (sys.equations.empty() || MaxAbs(f) < kTolerance) return true;
    if (cols.count == 0) return false;

    const int m = static_cast<int>(sys.equations.size());
    double mu = 1e-10;
    for (int iter = 0; iter < maxIterations; ++iter) {
        const Matrix J = Jacobian(sys, cols, sys.x);

        Matrix A(m, m);
        for (int i = 0; i < m; ++i)
            for (int j = 0; j <= i; ++j) {
                double s = 0.0;
                for (int k = 0; k < cols.count; ++k) s += J.At(i, k) * J.At(j, k);
                A.At(i, j) = s;
                A.At(j, i) = s;
            }
        for (int i = 0; i < m; ++i) A.At(i, i) += mu * (1.0 + A.At(i, i));

        std::vector<double> y = f;
        if (!SolveSpd(A, y)) {
            mu *= 100.0;
            continue;
        }
        std::vector<double> dx(static_cast<std::size_t>(cols.count), 0.0);
        for (int k = 0; k < cols.count; ++k)
            for (int i = 0; i < m; ++i) dx[static_cast<std::size_t>(k)] -= J.At(i, k) * y[static_cast<std::size_t>(i)];

        // Backtracking: accept the first step length that reduces the error.
        const double before = SumSquares(f);
        bool accepted = false;
        for (double step = 1.0; step > 1e-4; step *= 0.5) {
            std::vector<double> trial = sys.x;
            for (std::size_t u = 0; u < trial.size(); ++u) {
                const int col = cols.ofUnknown[u];
                if (col >= 0) trial[u] += step * dx[static_cast<std::size_t>(col)];
            }
            std::vector<double> fTrial = Residuals(sys, trial);
            if (SumSquares(fTrial) < before) {
                sys.x = std::move(trial);
                f = std::move(fTrial);
                accepted = true;
                break;
            }
        }
        if (MaxAbs(f) < kTolerance) return true;
        mu = accepted ? std::max(mu * 0.1, 1e-12) : mu * 100.0;
        if (mu > 1e6) return false;
    }
    return MaxAbs(f) < kTolerance;
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

/// Degrees of freedom of a system at its current values.
int DegreesOfFreedom(const EquationSystem& sys, std::vector<bool>* determinedUnknowns = nullptr) {
    const Columns cols(sys);
    std::vector<bool> determinedCols;
    const int rank = Reduce(Jacobian(sys, cols, sys.x), determinedCols);
    if (determinedUnknowns) {
        determinedUnknowns->assign(sys.x.size(), true);   // fixed unknowns are determined
        for (std::size_t u = 0; u < sys.x.size(); ++u) {
            const int col = cols.ofUnknown[u];
            if (col >= 0) (*determinedUnknowns)[u] = determinedCols[static_cast<std::size_t>(col)];
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
    if (options.drag) sys.Pin(options.drag->point, options.drag->target);
    bool ok = Iterate(sys, options.maxIterations);

    if (!ok && options.drag) {
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
