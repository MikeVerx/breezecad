// Audit harness: targeted robustness probes for core/sketch/document.
// Each probe runs in a forked child so a crash in one does not stop the rest.
#include "core/Expression.h"
#include "core/Units.h"
#include "document/BzcFile.h"
#include "document/Document.h"
#include "document/Recompute.h"
#include "document/SketchFeature.h"
#include "sketch/Solver.h"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>

using namespace bz;

static void Probe(const char* name, const std::function<void()>& body) {
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        try {
            body();
            std::printf("  [ok]     %s\n", name);
        } catch (const std::exception& e) {
            std::printf("  [THROWS] %s -> %s\n", name, e.what());
        }
        std::fflush(stdout);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) std::printf("  [CRASH]  %s -> signal %d\n", name, WTERMSIG(status));
    else if (WEXITSTATUS(status) != 0) std::printf("  [CRASH]  %s -> exit %d (sanitizer)\n", name, WEXITSTATUS(status));
}

static void Load(const std::string& json) {
    auto doc = doc::DocumentFromJson(json);
    if (!doc) {
        std::printf("           (rejected: %s)\n", doc.GetError().message.c_str());
        return;
    }
    doc::RecomputeModel(*doc);
    (void)doc::DocumentToJson(*doc);
}

static double Eval(const std::string& text) {
    UnitSystem units;
    auto r = EvaluateExpression(text, Quantity::Length, units, nullptr);
    return r ? *r : -1;
}

int main() {
    std::printf("== Document loading: wrong JSON types\n");
    Probe("part.name is a number", [] { Load(R"({"part":{"name":5}})"); });
    Probe("units is a string", [] { Load(R"({"units":"mm"})"); });
    Probe("feature is a number", [] { Load(R"({"features":[5]})"); });
    Probe("feature.type is a number", [] { Load(R"({"features":[{"type":7,"id":1}]})"); });
    Probe("nextId is a string", [] { Load(R"({"nextId":"9"})"); });
    Probe("variables is an object", [] { Load(R"({"variables":{"a":1}})"); });
    Probe("sketch points is a number",
          [] { Load(R"({"features":[{"type":"sketch","id":1,"name":"s","points":5}]})"); });
    Probe("sketch curve type is a number",
          [] { Load(R"({"features":[{"type":"sketch","id":1,"name":"s","curves":[{"type":3}]}]})"); });
    Probe("text corners is a number",
          [] { Load(R"({"features":[{"type":"sketch","id":1,"name":"s","curves":[{"type":"text","id":2,"text":"a","height":5,"corners":5,"sides":5}]}]})"); });

    std::printf("== Document loading: dangling references\n");
    Probe("arc whose points do not exist", [] {
        Load(R"({"features":[{"type":"sketch","id":1,"name":"s","points":[{"id":2,"pos":[0,0]}],
                 "curves":[{"type":"arc","id":5,"center":2,"start":77,"end":78}]}]})");
    });
    Probe("ellipse whose points do not exist", [] {
        Load(R"({"features":[{"type":"sketch","id":1,"name":"s","points":[{"id":2,"pos":[0,0]}],
                 "curves":[{"type":"ellipse","id":5,"center":2,"major":77,"minor":78}]}]})");
    });
    Probe("line whose points do not exist + length", [] {
        Load(R"({"features":[{"type":"sketch","id":1,"name":"s",
                 "curves":[{"type":"line","id":5,"start":77,"end":78}],
                 "constraints":[{"type":"length","id":9,"refs":[5],"parameter":{"name":"d0","expression":"10","value":10}}]}]})");
    });
    Probe("spline whose points do not exist", [] {
        Load(R"({"features":[{"type":"sketch","id":1,"name":"s",
                 "curves":[{"type":"spline","id":5,"points":[77,78,79]}]}]})");
    });
    Probe("nextId = 2^64-1", [] { Load(R"({"nextId":18446744073709551615})"); });
    Probe("endMarker huge", [] { Load(R"({"endMarker":999999,"features":[]})"); });

    std::printf("== Expressions\n");
    Probe("10k nested parentheses", [] { (void)Eval(std::string(10000, '(') + "1" + std::string(10000, ')')); });
    Probe("200k nested parentheses", [] { (void)Eval(std::string(200000, '(') + "1" + std::string(200000, ')')); });
    Probe("200k unary minus", [] { (void)Eval(std::string(200000, '-') + "1"); });
    Probe("power chain 2^2^...", [] {
        std::string s = "2";
        for (int i = 0; i < 100000; ++i) s += "^1";
        (void)Eval(s);
    });
    Probe("mm^1e300 (int overflow)", [] {
        UnitSystem u;
        (void)EvaluateExpression("(1mm)^1e300", Quantity::Scalar, u, nullptr);
    });
    Probe("round(2.5in) in inch units", [] {
        UnitSystem u;
        u.length = LengthUnit::Inch;
        auto r = EvaluateExpression("round(2.5)", Quantity::Length, u, nullptr);
        std::printf("           round(2.5) as inch length -> %.4f mm (2.5in=63.5mm)\n", r ? *r : -1);
    });

    std::printf("== Solver scaling (rectangles, fully dimensioned)\n");
    for (int n : {10, 25, 50, 100, 150}) {
        doc::SketchFeature f;
        IdGenerator ids;
        sketch::Sketch& sk = f.sketch;
        sk.EnsureOrigin(ids);
        for (int i = 0; i < n; ++i) {
            const double x = i * 30.0;
            ObjectId p[4] = {sk.AddPoint(ids, {x, 0}), sk.AddPoint(ids, {x + 20, 0}), sk.AddPoint(ids, {x + 20, 10}),
                             sk.AddPoint(ids, {x, 10})};
            ObjectId l[4];
            for (int k = 0; k < 4; ++k) l[k] = sk.AddLine(ids, p[k], p[(k + 1) % 4]);
            for (int k = 0; k < 4; ++k) {
                sketch::Constraint c;
                c.type = k % 2 ? sketch::ConstraintType::Vertical : sketch::ConstraintType::Horizontal;
                c.refs = {l[k], {}, {}};
                sk.AddConstraint(ids, c);
            }
            sketch::Constraint w;
            w.type = sketch::ConstraintType::Length;
            w.refs = {l[0], {}, {}};
            w.parameter.value = 21;
            sk.AddConstraint(ids, w);
        }
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = sketch::Solve(sk);
        const auto t1 = std::chrono::steady_clock::now();
        (void)sketch::Solve(sk);   // already solved: what every rebuild pays
        const auto t2 = std::chrono::steady_clock::now();
        std::printf("  %4d rects (%4zu points, %4zu constraints): solve %.1f ms, re-solve %.1f ms, dof %d\n", n,
                    sk.points.size(), sk.constraints.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count(),
                    std::chrono::duration<double, std::milli>(t2 - t1).count(), r.degreesOfFreedom);
    }

    std::printf("== Angle constraint with a huge value (hang check, 5 s alarm)\n");
    for (double deg : {1e6, 1e9, 1e15}) {
        char name[64];
        std::snprintf(name, sizeof name, "angle = %g deg", deg);
        Probe(name, [deg] {
            alarm(5);
            sketch::Sketch sk;
            IdGenerator ids;
            const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 0}), c = sk.AddPoint(ids, {0, 0.1}), d = sk.AddPoint(ids, {10, 5});
            const ObjectId l1 = sk.AddLine(ids, a, b), l2 = sk.AddLine(ids, c, d);
            sketch::Constraint k;
            k.type = sketch::ConstraintType::Angle;
            k.refs = {l1, l2, {}};
            k.parameter.quantity = Quantity::Angle;
            k.parameter.value = deg * 3.14159265358979 / 180.0;
            sk.AddConstraint(ids, k);
            const auto t0 = std::chrono::steady_clock::now();
            const auto r = sketch::Solve(sk);
            std::printf("           converged=%d in %.0f ms\n", r.converged,
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        });
    }
    return 0;
}
