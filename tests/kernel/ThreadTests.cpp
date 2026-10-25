#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/ThreadFeature.h"
#include "kernel/Model.h"
#include "kernel/occt/Thread.h"

#include <nlohmann/json.hpp>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

doc::ThreadFeature& AddThread(doc::Document& d, double radius, const std::string& pitch) {
    auto t = std::make_unique<doc::ThreadFeature>();
    t->id = d.Ids().Next();
    t->name = d.UniqueFeatureName("Резьба");
    t->face = doc::CylinderTouch{{0, 0, 0}, {0, 0, 1}, radius, {1, 0, 0}};
    t->pitch.expression = pitch;
    auto* raw = t.get();
    d.Insert(std::move(t), d.FeatureCount());
    return *raw;
}

} // namespace

TEST_CASE("an external thread cuts a groove along the shaft") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddCircle(d, sk, {0, 0}, 4);
    AddExtrude(d, sk.id, "20");
    auto& thread = AddThread(d, 4, "1.25");
    kernel::Model model;
    model.Rebuild(d);
    INFO(thread.status.message);
    REQUIRE(thread.status.state == FeatureState::Ok);
    const double shaft = kPi * 16 * 20;
    // ISO 68-1 groove: a trapezoid 7/8 P wide at the surface, 0.6134 P deep,
    // flanks at 60°: 0.3195 P² in section, its centre of area 0.237 P below
    // the surface; 16 turns along the 20 mm. A groove of the right width puts
    // the pitch diameter where it belongs (the old V groove was too narrow:
    // about 125 mm³ here, a bolt too fat for a nut).
    const double area = 0.3195 * 1.25 * 1.25;
    const double expected = area * 2 * kPi * (4 - 0.237 * 1.25) * 16;
    const double cut = shaft - model.Volume();
    CHECK(cut == doctest::Approx(expected).epsilon(0.08));

    thread.fullLength = false;
    thread.length.expression = "10";
    d.MarkChanged(2);
    model.Rebuild(d);
    INFO(thread.status.message);
    REQUIRE(thread.status.state == FeatureState::Ok);
    CHECK(shaft - model.Volume() == doctest::Approx(cut / 2).epsilon(0.15));
}

TEST_CASE("an internal thread cuts into the hole wall") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-10, -10}, {10, 10});
    AddCircle(d, sk, {0, 0}, 3.4);
    AddExtrude(d, sk.id, "12");
    auto& thread = AddThread(d, 3.4, "1.25");
    thread.leftHanded = true;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(thread.status.state == FeatureState::Ok);
    const double plate = (400 - kPi * 3.4 * 3.4) * 12;
    CHECK(plate - model.Volume() > 30);
    CHECK(plate - model.Volume() < 150);
    CHECK(doc::CoarsePitch(8) == 1.25);
    CHECK(doc::CoarsePitch(6.1) == 1.0);
    REQUIRE(thread.site);
    CHECK_FALSE(thread.site->external);
    CHECK(thread.site->nominal == 8);
    CHECK(doc::StandardSize(7.97) == 8);
    CHECK(doc::StandardSize(7) == 7);
}

TEST_CASE("a thread without a cylinder reports it") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-10, -10}, {10, 10});
    AddExtrude(d, sk.id, "12");
    auto& thread = AddThread(d, 4, "1.25");
    kernel::Model model;
    model.Rebuild(d);
    CHECK(thread.status.state == FeatureState::Error);
}

TEST_CASE("a thread survives saving and loading") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddCircle(d, sk, {0, 0}, 4);
    AddExtrude(d, sk.id, "20");
    auto& thread = AddThread(d, 4, "1.25");
    thread.fullLength = false;
    thread.fromOtherEnd = true;
    thread.leftHanded = true;
    thread.length.expression = "7";
    const auto loaded = doc::DocumentFromJson(doc::DocumentToJson(d));
    REQUIRE(loaded);
    const auto* t = dynamic_cast<const doc::ThreadFeature*>(loaded->Features()[2].get());
    REQUIRE(t);
    REQUIRE(t->face);
    CHECK(t->face->radius == 4);
    CHECK_FALSE(t->fullLength);
    CHECK(t->fromOtherEnd);
    CHECK(t->leftHanded);
    CHECK(t->length.expression == "7");
}

TEST_CASE("a thread stops short of a shoulder and runs out of a free end") {
    const auto cutOf = [](bool onPlate) {
        doc::Document d;
        if (onPlate) {
            auto& plate = AddSketch(d);
            AddRect(d, plate, {-10, -10}, {10, 10});
            AddExtrude(d, plate.id, "5");
        }
        auto& sk = AddSketch(d);
        AddCircle(d, sk, {0, 0}, 4);
        AddExtrude(d, sk.id, onPlate ? "25" : "20", onPlate ? doc::BooleanOp::Join : doc::BooleanOp::NewBody);
        kernel::Model before;
        before.Rebuild(d);
        auto& thread = AddThread(d, 4, "1.25");
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        return before.Volume() - model.Volume();
    };
    CHECK(cutOf(true) < cutOf(false) - 1);
}

TEST_CASE("a chosen standard size wins over the shaft, a cosmetic thread cuts nothing") {
    // An 8.6 mm shaft with M8 chosen: cut down to 8 mm, then threaded, so
    // it ends up the same as an M8 thread on an 8 mm shaft.
    const auto threaded = [](double radius, double size, bool cosmetic) {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddCircle(d, sk, {0, 0}, radius);
        AddExtrude(d, sk.id, "20");
        auto& thread = AddThread(d, radius, "1.25");
        thread.size = size;
        thread.cosmetic = cosmetic;
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        return model.Volume();
    };
    const double m8 = threaded(4, 0, false);
    CHECK(threaded(4.3, 8, false) == doctest::Approx(m8).epsilon(0.01));
    CHECK(threaded(4, 0, true) == doctest::Approx(kPi * 16 * 20).epsilon(1e-6));

    doc::Document d;
    auto& sk = AddSketch(d);
    AddCircle(d, sk, {0, 0}, 4);
    AddExtrude(d, sk.id, "20");
    auto& thread = AddThread(d, 4, "1.25");
    thread.size = 8;
    thread.cosmetic = true;
    const auto loaded = doc::DocumentFromJson(doc::DocumentToJson(d));
    REQUIRE(loaded);
    const auto* t = dynamic_cast<const doc::ThreadFeature*>(loaded->Features()[2].get());
    REQUIRE(t);
    CHECK(t->size == 8);
    CHECK(t->cosmetic);
    CHECK(doc::StandardSizes().size() == 22);
}

TEST_CASE("a size bigger than the shaft or smaller than the hole comes out whole, not cut short") {
    // A 6 mm shaft with M8 chosen: material grows to 8 mm first, then the thread.
    {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddCircle(d, sk, {0, 0}, 3);
        AddExtrude(d, sk.id, "20");
        auto& thread = AddThread(d, 3, "1.25");
        thread.size = 8;
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        doc::Document ref;
        auto& rs = AddSketch(ref);
        AddCircle(ref, rs, {0, 0}, 4);
        AddExtrude(ref, rs.id, "20");
        AddThread(ref, 4, "1.25");
        kernel::Model m8;
        m8.Rebuild(ref);
        CHECK(model.Volume() == doctest::Approx(m8.Volume()).epsilon(0.01));
    }
    // A 10 mm hole with M6 chosen: the wall grows in to the minor diameter.
    {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddRect(d, sk, {-10, -10}, {10, 10});
        AddCircle(d, sk, {0, 0}, 5);
        AddExtrude(d, sk.id, "12");
        auto& thread = AddThread(d, 5, "1");
        thread.size = 6;
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        const double minor = 6 - 1.0825;
        const double plate = 400 * 12 - kPi * minor * minor / 4 * 12;
        // Less the groove: 0.2368 P² in section, about 9 mm³ a turn round 6 mm.
        CHECK(plate - model.Volume() > 20);
        CHECK(plate - model.Volume() < 70);
    }
}

// --- Kinds, fits and lead-in chamfers (0.2.4.2) ------------------------------

TEST_CASE("thread kinds: basic profiles and series") {
    using doc::ThreadStandard;
    // Metric M8×1.25: the nut's minor diameter is D - 1.0825 P (ISO 68-1).
    const doc::ThreadProfile m = doc::BasicProfile(ThreadStandard::Metric, 1.25, false);
    CHECK(8 - 2 * m.minorOffset == doctest::Approx(8 - 1.0825 * 1.25).epsilon(1e-3));
    // G1/2: 14 threads per inch, depth 0.640327 P, 55°.
    CHECK(doc::StandardPitch(ThreadStandard::PipeG, 20.955) == doctest::Approx(25.4 / 14));
    CHECK(doc::PipeSizeName(20.955) == "G1/2");
    const doc::ThreadProfile g = doc::BasicProfile(ThreadStandard::PipeG, 25.4 / 14, true);
    CHECK(g.depth == doctest::Approx(0.640327 * 25.4 / 14));
    CHECK(2 * g.halfAngle * 180 / kPi == doctest::Approx(55));
    // Tr16×4: depth P/2 + ac (0.25), crest flats 0.366P, 30°; nut minor d - P.
    CHECK(doc::StandardPitch(ThreadStandard::Trapezoidal, 16) == 4);
    const doc::ThreadProfile tr = doc::BasicProfile(ThreadStandard::Trapezoidal, 4, true);
    CHECK(tr.depth == doctest::Approx(2.25));
    CHECK(tr.mouth == doctest::Approx(4 - 0.366 * 4));
    CHECK(16 - 2 * doc::BasicProfile(ThreadStandard::Trapezoidal, 4, false).minorOffset == doctest::Approx(12));
    CHECK(doc::NearestStandardSize(ThreadStandard::Trapezoidal, 15.8) == 16);
    CHECK(doc::NearestStandardSize(ThreadStandard::PipeG, 21) == doctest::Approx(20.955));
}

TEST_CASE("fits move the profile away from the counterpart, flatten crests, deepen roots") {
    doc::ThreadSpec spec;
    const doc::ThreadCut nominalBolt = doc::CutOf(spec, 8, 1.25, true);
    CHECK(nominalBolt.anchor == doctest::Approx(4));
    CHECK(nominalBolt.surface == doctest::Approx(4));
    spec.fit = doc::ThreadFit::Free;
    const doc::ThreadCut bolt = doc::CutOf(spec, 8, 1.25, true);
    const doc::ThreadCut nut = doc::CutOf(spec, 8, 1.25, false);
    CHECK(bolt.anchor == doctest::Approx(4 - 0.15));        // 0.3 off the pitch diameter
    CHECK(bolt.surface == doctest::Approx(4 - 0.25));       // crests 0.1 lower
    CHECK(bolt.depth > nominalBolt.depth);
    CHECK(nut.anchor == doctest::Approx(4 - 0.5413 * 1.25 + 0.15));
    // The same flank gap on a 30° thread needs a bigger shift.
    spec.standard = doc::ThreadStandard::Trapezoidal;
    CHECK(doc::AllowanceOf(spec).shift == doctest::Approx(0.075 / std::sin(kPi / 12)));
    // A custom clearance is on the pitch diameter, whatever the angle.
    spec.fit = doc::ThreadFit::Custom;
    spec.clearance.value = 0.4;
    CHECK(doc::AllowanceOf(spec).shift == doctest::Approx(0.2));
    // Saved and read back; a 0.2.4.1 file (no "thread") has no chamfer.
    spec.leadChamfer = false;
    doc::ThreadSpec back;
    REQUIRE(doc::ReadThreadSpec(doc::ToJson(spec), back));
    CHECK(back.standard == doc::ThreadStandard::Trapezoidal);
    CHECK(back.fit == doc::ThreadFit::Custom);
    CHECK_FALSE(back.leadChamfer);
    doc::ThreadSpec fresh;
    CHECK(fresh.leadChamfer);
}

namespace {

/// `blank` with the thread of `spec` on its cylinder `face` (as the model cuts it).
TopoDS_Shape Threaded(const TopoDS_Shape& blank, doc::CylinderTouch face, double size, double pitch, const doc::ThreadSpec& spec) {
    doc::ThreadFeature f;
    f.face = face;
    f.size = size;
    f.pitch.value = pitch;
    f.spec = spec;
    const Expected<kernel::occt::ThreadShapes> tool = kernel::occt::ThreadTool(blank, f);
    REQUIRE(tool);
    TopoDS_Shape shape = blank;
    if (tool->fill) shape = BRepAlgoAPI_Fuse(shape, *tool->fill).Shape();
    if (tool->chamfer) shape = BRepAlgoAPI_Cut(shape, *tool->chamfer).Shape();
    return BRepAlgoAPI_Cut(shape, tool->cut).Shape();
}

double VolumeOf(const TopoDS_Shape& s) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(s, props);
    return props.Mass();
}

/// A bolt (nominal `d`, from z 0 to 14) and a nut (a 30 mm block from z 4
/// to 10) of the same kind and pitch, each with its own fit. The bolt is
/// screwed to the turn where it sits best in the nut (the middle of its
/// play); returns the gap left between them there, mm (0: they touch or
/// cut into each other).
double MatedGap(doc::ThreadStandard standard, double d, double pitch, doc::ThreadFit boltFit, doc::ThreadFit nutFit) {
    doc::ThreadSpec boltSpec, nutSpec;
    boltSpec.standard = nutSpec.standard = standard;
    boltSpec.fit = boltFit;
    nutSpec.fit = nutFit;
    const TopoDS_Shape bolt = Threaded(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), d / 2, 14).Shape(),
                                       {{0, 0, 0}, {0, 0, 1}, d / 2, {1, 0, 0}}, d, pitch, boltSpec);
    const double minor = d / 2 - doc::BasicProfile(standard, pitch, false).minorOffset;
    const TopoDS_Shape block = BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-15, -15, 4), 30, 30, 6).Shape(),
                                               BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), minor, 20).Shape())
                                   .Shape();
    const TopoDS_Shape nut = Threaded(block, {{0, 0, 0}, {0, 0, 1}, minor, {1, 0, 0}}, d, pitch, nutSpec);
    // Screwing the bolt is moving it along the axis (one helix). At each
    // shift: the gap between them, or, where they cut into each other, minus
    // the volume they share — so the search sees which way is better even
    // where the gap is zero (the window of play can be narrower than a step).
    const auto score = [&](double shift) {
        gp_Trsf t;
        t.SetTranslation(gp_Vec(0, 0, shift));
        const TopoDS_Shape moved = BRepBuilderAPI_Transform(bolt, t, true).Shape();
        BRepExtrema_DistShapeShape distance(moved, nut);
        if (distance.IsDone() && !distance.InnerSolution() && distance.Value() > 1e-7) return distance.Value();
        return -VolumeOf(BRepAlgoAPI_Common(moved, nut).Shape());
    };
    constexpr int kSamples = 8;
    double best = -1e300, bestShift = 0;
    for (int i = 0; i < kSamples; ++i) {
        const double shift = pitch * i / kSamples;
        if (const double g = score(shift); g > best) {
            best = g;
            bestShift = shift;
        }
    }
    double lo = bestShift - pitch / kSamples, hi = bestShift + pitch / kSamples;
    for (int i = 0; i < 9; ++i) {   // golden section on the peak
        const double a = hi - (hi - lo) * 0.618, b = lo + (hi - lo) * 0.618;
        if (score(a) > score(b)) hi = b;
        else lo = a;
    }
    return std::max({best, score((lo + hi) / 2), 0.0});
}

} // namespace

TEST_CASE("a bolt and a nut made here screw together with the gap their fits promise") {
    using doc::ThreadFit;
    using doc::ThreadStandard;
    // Within 0.015 mm of the promise (the search and the solids' tolerances).
    const auto near = [](double gap, double promised) { return std::abs(gap - promised) < 0.015; };
    // As in the book: no gap at all (a metal pair meets "in zero"; printed,
    // it would not turn).
    CHECK(MatedGap(ThreadStandard::Metric, 8, 1.25, ThreadFit::Nominal, ThreadFit::Nominal) < 0.01);
    // Printed to printed: 0.075 mm across each flank from each part (free),
    // half that (snug).
    CHECK(near(MatedGap(ThreadStandard::Metric, 8, 1.25, ThreadFit::Free, ThreadFit::Free), 0.15));
    CHECK(near(MatedGap(ThreadStandard::Metric, 8, 1.25, ThreadFit::Snug, ThreadFit::Snug), 0.075));
    // A printed nut on a metal bolt (here the basic one): 0.05 from the printed side.
    CHECK(near(MatedGap(ThreadStandard::Metric, 8, 1.25, ThreadFit::Nominal, ThreadFit::Metal), 0.05));
    // The same flank gap whatever the thread angle.
    CHECK(near(MatedGap(ThreadStandard::Trapezoidal, 16, 4, ThreadFit::Free, ThreadFit::Free), 0.15));
    CHECK(near(MatedGap(ThreadStandard::PipeG, 20.955, 25.4 / 14, ThreadFit::Free, ThreadFit::Free), 0.15));
}

TEST_CASE("the lead-in chamfer takes the first crests off, and can be turned off") {
    doc::ThreadSpec with, without;
    without.leadChamfer = false;
    const TopoDS_Shape blank = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 4, 14).Shape();
    const doc::CylinderTouch face{{0, 0, 0}, {0, 0, 1}, 4, {1, 0, 0}};
    const double plain = VolumeOf(Threaded(blank, face, 8, 1.25, without));
    const double chamfered = VolumeOf(Threaded(blank, face, 8, 1.25, with));
    // Both ends are free: two rings of 45°, 0.77 mm deep, about half of each is thread already cut.
    CHECK(chamfered < plain - 1.0);
    CHECK(chamfered > plain - 20.0);
}
