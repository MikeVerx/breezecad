// A whole part made of most operations, changed afterwards: the integration
// check that the operations work together and follow a change upstream.
// With BZ_SAMPLE_OUT set, the part is also saved there (to open in the app).

#include "../Fixtures.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/EdgeFeature.h"
#include "document/HoleFeature.h"
#include "document/PatternFeature.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <doctest.h>

#include <cstdlib>
#include <functional>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

template <class T>
T& AddFeature(doc::Document& d, const std::string& name) {
    auto f = std::make_unique<T>();
    f->id = d.Ids().Next();
    f->name = d.UniqueFeatureName(name);
    auto* raw = f.get();
    d.Insert(std::move(f), d.FeatureCount());
    return *raw;
}

doc::HoleFeature& AddHole(doc::Document& d, doc::FaceReference face, Vec2 centre, const std::string& diameter) {
    auto& h = AddFeature<doc::HoleFeature>(d, "Отверстие");
    h.face = face;
    h.centres = {centre};
    h.throughAll = true;
    h.diameter.expression = diameter;
    return h;
}

/// References to the straight edges of the last body that `keep` accepts.
std::vector<doc::EdgeReference> Edges(const kernel::Model& model, const std::function<bool(gp_Pnt, gp_Dir)>& keep) {
    const TopoDS_Shape& body = model.GetImpl().bodies.back().shape;
    std::vector<doc::EdgeReference> refs;
    TopTools_IndexedMapOfShape edges;   // each edge once (an explorer meets it on both faces)
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(edges(i));
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Line) continue;
        const gp_Pnt mid = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2);
        if (keep(mid, curve.Line().Direction())) refs.push_back(kernel::occt::DescribeEdge(edge, body));
    }
    return refs;
}

bool AllOk(const doc::Document& d) {
    bool ok = true;
    for (const auto& f : d.Features())
        if (f->status.state != FeatureState::Ok) {
            MESSAGE(f->name << ": " << f->status.message);
            ok = false;
        }
    return ok;
}

} // namespace

TEST_CASE("bracket: plate, boss on its face, holes, pattern, fillets, chamfer") {
    doc::Document d;
    kernel::Model model;

    auto& base = AddSketch(d);
    AddRect(d, base, {-60, -40}, {60, 40});
    auto& plate = AddExtrude(d, base.id, "10");

    auto& top = AddSketch(d);
    top.placement = {plate.id, {{0, 0, 10}, {1, 0, 0}, {0, 0, 1}}, true, {{0, 0, 1}, {0, 0, 10}}};
    AddRect(d, top, {-20, -20}, {20, 20});
    AddExtrude(d, top.id, "30", doc::BooleanOp::Join);

    AddHole(d, {{0, 0, 1}, {0, 0, 40}}, {0, 0}, "12");                    // through the boss
    auto& corner = AddHole(d, {{0, 0, 1}, {50, 30, 10}}, {50, 30}, "8");   // a corner of the plate

    auto& pattern = AddFeature<doc::PatternFeature>(d, "Массив");
    pattern.sources = {corner.id};
    pattern.reversed1 = true;
    pattern.count1.expression = "2";
    pattern.spacing1.expression = "100";
    pattern.useDirection2 = true;
    pattern.reversed2 = true;
    pattern.count2.expression = "2";
    pattern.spacing2.expression = "60";

    model.Rebuild(d);
    REQUIRE(AllOk(d));

    // Round the four vertical corners of the plate, bevel the top of the boss.
    auto& fillet = AddFeature<doc::FilletFeature>(d, "Сопряжение");
    fillet.radius.expression = "5";
    fillet.edges = Edges(model, [](gp_Pnt p, gp_Dir dir) {
        return std::abs(dir.Z()) > 0.99 && std::abs(std::abs(p.X()) - 60) < 1e-6 && std::abs(std::abs(p.Y()) - 40) < 1e-6;
    });
    REQUIRE(fillet.edges.size() == 4);
    model.Rebuild(d);
    auto& chamfer = AddFeature<doc::ChamferFeature>(d, "Фаска");
    chamfer.distance.expression = "2";
    chamfer.edges = Edges(model, [](gp_Pnt p, gp_Dir dir) {
        return std::abs(dir.Z()) < 0.01 && std::abs(p.Z() - 40) < 1e-6 && (std::abs(std::abs(p.X()) - 20) < 1e-6 ||
                                                                            std::abs(std::abs(p.Y()) - 20) < 1e-6);
    });
    REQUIRE(chamfer.edges.size() == 4);
    auto& above = AddFeature<doc::WorkPlaneFeature>(d, "Плоскость");
    above.base = doc::origin::kPlaneXY;
    above.offset.expression = "60";

    model.Rebuild(d);
    REQUIRE(AllOk(d));
    const double volume = model.Volume();
    CHECK(model.BoundingBox()->max.z == doctest::Approx(40.0).epsilon(1e-4));

    // A thicker plate: the boss rides up on its face, every hole still goes
    // through, the fillets and the chamfer find their edges again.
    plate.distance.expression = "15";
    d.MarkChanged(1);
    model.Rebuild(d);
    REQUIRE(AllOk(d));
    CHECK(model.BoundingBox()->max.z == doctest::Approx(45.0).epsilon(1e-4));
    CHECK(model.Volume() > volume);

    // Saved and opened again, it builds the same.
    const auto dir = std::filesystem::temp_directory_path() / "bz_bracket_tests";
    std::filesystem::create_directories(dir);
    REQUIRE(doc::SaveBzc(d, dir / "bracket.bzc", "tests").IsOk());
    doc::LoadReport report;
    auto loaded = doc::LoadBzc(dir / "bracket.bzc", &report);
    std::filesystem::remove_all(dir);
    REQUIRE(loaded);
    for (const std::string& w : report.warnings) MESSAGE(w);
    CHECK(report.warnings.empty());
    kernel::Model reopened;
    reopened.Rebuild(*loaded);
    CHECK(AllOk(*loaded));
    CHECK(reopened.Volume() == doctest::Approx(model.Volume()));

    if (const char* out = std::getenv("BZ_SAMPLE_OUT")) REQUIRE(doc::SaveBzc(d, PathFromUtf8(out), "tests").IsOk());
}
