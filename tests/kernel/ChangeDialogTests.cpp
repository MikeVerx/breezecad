// «Обнаружено изменение, затрагивающее другие детали» (ADR-0009, part 2):
// a box built around a plate in an assembly; the plate's own sketch changes.
#include "../Fixtures.h"
#include "assembly/AssemblyFile.h"
#include "document/BzcFile.h"
#include "document/EdgeFeature.h"
#include "document/Recompute.h"
#include "kernel/AssemblyModel.h"
#include "kernel/ContextProjection.h"
#include "kernel/Model.h"
#include "kernel/Projection.h"

#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;

namespace {

/// 40 x 40 x 10 plate from the origin up.
doc::Document Plate() {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    test::AddRect(d, sk, {0, 0}, {40, 40});
    test::AddExtrude(d, sk.id, "10");
    return d;
}

struct MemoryPart {
    doc::Document doc;
    kernel::Model model;
    void Rebuild() { model.Rebuild(doc); }
};

/// The plate (in memory, to be changed) and a box whose sketch, on the
/// plate's top, projects the plate's four top edges.
struct Bench {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz_change_dialog";
    std::filesystem::path file = dir / "Узел.bza";
    const std::string     name = "Узел";
    Assembly              a;
    kernel::AssemblyModel m;
    MemoryPart            plate{Plate(), {}};
    MemoryPart            box;
    ObjectId              plateId, boxId;
    std::vector<ObjectId> curves;   ///< the box's projected curves: y 0, x 40, y 40, x 0

    Bench() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        plateId = a.AddComponent("Основание.bzc", "Основание").id;
        plate.Rebuild();
        m.UsePartInMemory(dir / "Основание.bzc", &plate.doc, &plate.model);
        test::AddSketch(box.doc);
        boxId = a.AddComponent("Короб.bzc", "Короб", {Rotation{}, {0, 0, 10}}).id;
        box.Rebuild();
        m.UsePartInMemory(dir / "Короб.bzc", &box.doc, &box.model);
        m.Update(a, file);
        for (Vec3 at : {Vec3{20, 0, 10}, Vec3{40, 20, 10}, Vec3{20, 40, 10}, Vec3{0, 20, 10}}) curves.push_back(Project(at));
        box.Rebuild();
        m.Update(a, file);
        REQUIRE(kernel::AdaptContextProjections(m, a, boxId, box.doc, name).lost == 0);   // sources learnt
    }
    ~Bench() { std::filesystem::remove_all(dir); }

    doc::SketchFeature& BoxSketch() { return *dynamic_cast<doc::SketchFeature*>(box.doc.Features()[0].get()); }
    doc::SketchFeature& PlateSketch() { return *dynamic_cast<doc::SketchFeature*>(plate.doc.Features()[0].get()); }

    ObjectId Project(Vec3 worldPoint) {
        doc::SketchFeature& sk = BoxSketch();
        const std::optional<doc::EdgeReference> edge = m.EdgeNear(a, plateId, worldPoint, 1.0);
        REQUIRE(edge);
        doc::ContextProjection link{name, plateId, *edge, {}, true, {}};
        const Expected<kernel::ProjectedCurve> pc = m.ProjectSourceEdge(a, boxId, link, sk.placement.frame);
        REQUIRE(pc);
        REQUIRE(pc->kind == kernel::ProjectedCurve::Kind::Line);
        IdGenerator& ids = box.doc.Ids();
        const ObjectId p0 = sk.sketch.AddPoint(ids, pc->start), p1 = sk.sketch.AddPoint(ids, pc->end);
        sk.sketch.FindPoint(p0)->projected = true;
        sk.sketch.FindPoint(p1)->projected = true;
        link.curve = sk.sketch.AddLine(ids, p0, p1);
        sk.sketch.FindLine(link.curve)->projected = true;
        sk.contextProjections.push_back(link);
        box.doc.MarkChanged(0);
        return link.curve;
    }

    /// The plate rebuilt after a change of its document, shown so.
    void PlateChanged() {
        plate.doc.MarkChanged(0);
        plate.Rebuild();
        m.Update(a, file);
    }

    /// A 10 x 10 tab added to the plate's sketch on its right side (x 40,
    /// y 15..25): the right line now ends at y 15, five lines go round the tab.
    void AddTab() {
        sketch::Sketch& s = PlateSketch().sketch;
        IdGenerator& ids = plate.doc.Ids();
        sketch::Line* right = nullptr;
        for (sketch::Line& l : s.lines)
            if (std::abs(s.PositionOf(l.start).x - 40) < 1e-9 && std::abs(s.PositionOf(l.end).x - 40) < 1e-9) right = &l;
        REQUIRE(right);
        const bool up = s.PositionOf(right->end).y > s.PositionOf(right->start).y;
        const ObjectId top = up ? right->end : right->start;   // the corner at (40, 40)
        const ObjectId cut = s.AddPoint(ids, {40, 15});
        right = nullptr;
        for (sketch::Line& l : s.lines)
            if (l.start == top && std::abs(s.PositionOf(l.end).x - 40) < 1e-9) l.start = cut, right = &l;
            else if (l.end == top && std::abs(s.PositionOf(l.start).x - 40) < 1e-9) l.end = cut, right = &l;
        REQUIRE(right);
        const ObjectId a1 = s.AddPoint(ids, {50, 15}), a2 = s.AddPoint(ids, {50, 25}), a3 = s.AddPoint(ids, {40, 25});
        s.AddLine(ids, cut, a1);
        s.AddLine(ids, a1, a2);
        s.AddLine(ids, a2, a3);
        s.AddLine(ids, a3, top);
        PlateChanged();
    }
};

} // namespace

TEST_CASE("изменение эскиза детали-источника находит затронутый эскиз и кривую, скругление — нет") {
    Bench b;
    const kernel::SourcePrints before = kernel::SourcePrintsOf(b.m, b.box.doc, b.name);
    REQUIRE(before.size() == 4);
    for (const auto& [curve, print] : before) CHECK(print != kernel::kSourceGone);
    CHECK(kernel::FindAffectedSketches(b.box.doc, b.name, before, before).empty());

    SUBCASE("скругление ребра основания эскиз не трогает: короба в списке нет") {
        const std::optional<doc::EdgeReference> edge = b.m.EdgeNear(b.a, b.plateId, {40, 20, 10}, 1.0);
        REQUIRE(edge);
        auto f = std::make_unique<doc::FilletFeature>();
        f->radius.expression = "3";
        f->id = b.plate.doc.Ids().Next();
        f->name = "Скругление1";
        f->edges = {*edge};
        b.plate.doc.Insert(std::move(f), b.plate.doc.FeatureCount());
        b.PlateChanged();
        const kernel::SourcePrints now = kernel::SourcePrintsOf(b.m, b.box.doc, b.name);
        CHECK(kernel::FindAffectedSketches(b.box.doc, b.name, before, now).empty());
    }
    SUBCASE("сдвиг компонента — тоже не изменение источника") {
        b.a.FindComponent(b.boxId)->placement.translation = {5, 0, 10};
        b.a.Touch();
        b.m.Update(b.a, b.file);
        CHECK(kernel::FindAffectedSketches(b.box.doc, b.name, before, kernel::SourcePrintsOf(b.m, b.box.doc, b.name)).empty());
    }
    SUBCASE("к эскизу основания добавлен выступ: короб в списке, эскиз и кривая названы") {
        b.AddTab();
        const kernel::SourcePrints now = kernel::SourcePrintsOf(b.m, b.box.doc, b.name);
        const std::vector<kernel::AffectedSketch> hit = kernel::FindAffectedSketches(b.box.doc, b.name, before, now);
        REQUIRE(hit.size() == 1);
        CHECK(hit[0].sketch == b.BoxSketch().id);
        CHECK(hit[0].name == b.BoxSketch().name);
        REQUIRE(hit[0].curves.size() == 1);
        CHECK(hit[0].curves[0] == b.curves[1]);   // the right line, x 40
        CHECK(hit[0].sourceComponent == b.plateId);
        CHECK(hit[0].sourceSketch == b.PlateSketch().id);
    }
    SUBCASE("кривую-источник удалили: затронута") {
        sketch::Sketch& s = b.PlateSketch().sketch;
        const ObjectId bottom = b.BoxSketch().contextProjections[0].source.curve;
        s.Remove(bottom);
        b.PlateChanged();
        const kernel::SourcePrints now = kernel::SourcePrintsOf(b.m, b.box.doc, b.name);
        CHECK(now.at(b.curves[0]) == kernel::kSourceGone);
        const auto hit = kernel::FindAffectedSketches(b.box.doc, b.name, before, now);
        REQUIRE(hit.size() == 1);
        CHECK(std::find(hit[0].curves.begin(), hit[0].curves.end(), b.curves[0]) != hit[0].curves.end());
    }
}

namespace {

/// The box's outline: a rectangle 0.2 mm round the projected plate, tied to
/// it as an offset is (a distance from each side), and a hole dimensioned
/// from the projected left side.
struct Outline {
    std::vector<ObjectId> lines;
    ObjectId              holeCentre, holeDimension;
};
Outline DrawOutline(Bench& b, double gap = 0.2) {
    doc::SketchFeature& sk = b.BoxSketch();
    sketch::Sketch& s = sk.sketch;
    IdGenerator& ids = b.box.doc.Ids();
    const std::size_t from = s.lines.size();
    test::AddRect(b.box.doc, sk, {-gap, -gap}, {40 + gap, 40 + gap});
    Outline out;
    for (std::size_t i = from; i < s.lines.size(); ++i) out.lines.push_back(s.lines[i].id);
    // Each outline side 0.2 from the projected side it runs along.
    for (ObjectId projected : b.curves) {
        const sketch::Line* l = s.FindLine(projected);
        const Vec2 p = s.PositionOf(l->start);
        for (ObjectId side : out.lines) {
            const sketch::Line* o = s.FindLine(side);
            const Vec2 q0 = s.PositionOf(o->start), q1 = s.PositionOf(o->end), d = (q1 - q0) * (1.0 / (q1 - q0).Length());
            const Vec2 r = p - q0;
            if (std::abs(std::abs(r.x * d.y - r.y * d.x) - gap) > 0.01) continue;
            const Vec2 pd = s.PositionOf(s.FindLine(projected)->end) - p;
            if (std::abs(pd.x * d.y - pd.y * d.x) > 1e-6) continue;   // not parallel
            sketch::Constraint c;
            c.type = sketch::ConstraintType::PointLineDistance;
            c.refs = {l->start, side, {}};
            c.parameter.name = b.box.doc.NextParameterName();
            c.parameter.expression = std::format("{}", gap);
            s.AddConstraint(ids, c);
            break;
        }
    }
    out.holeCentre = s.AddPoint(ids, {10, 20});
    s.AddCircle(ids, out.holeCentre, 3);
    sketch::Constraint c;
    c.type = sketch::ConstraintType::PointLineDistance;
    c.refs = {out.holeCentre, b.curves[3], {}};   // 10 from the left side
    c.parameter.name = b.box.doc.NextParameterName();
    c.parameter.expression = "10";
    out.holeDimension = s.AddConstraint(ids, c);
    b.box.doc.EvaluateParameters();
    doc::SolveSketch(sk);
    REQUIRE(sk.analysis.failing.empty());
    return out;
}

} // namespace

TEST_CASE("«Игнорировать изменения»: затронутые проекции перестают следовать, деталь не меняется") {
    Bench b;
    const kernel::SourcePrints before = kernel::SourcePrintsOf(b.m, b.box.doc, b.name);
    b.AddTab();
    const auto hit = kernel::FindAffectedSketches(b.box.doc, b.name, before, kernel::SourcePrintsOf(b.m, b.box.doc, b.name));
    REQUIRE(hit.size() == 1);
    const sketch::Sketch was = b.BoxSketch().sketch;
    CHECK(kernel::IgnoreSourceChange(b.box.doc, b.name, hit) == 1);
    CHECK_FALSE(b.BoxSketch().contextProjections[1].adaptive);
    CHECK(b.BoxSketch().contextProjections[0].adaptive);
    CHECK(b.BoxSketch().sketch.lines.size() == was.lines.size());
    // Not found again: that projection follows nothing now.
    CHECK(kernel::FindAffectedSketches(b.box.doc, b.name, before, kernel::SourcePrintsOf(b.m, b.box.doc, b.name)).empty());
}

TEST_CASE("«Мимикрирование с зазором»: новый контур повторён эквидистантой, зависимости перенесены") {
    Bench b;
    const Outline outline = DrawOutline(b);
    const kernel::SourcePrints before = kernel::SourcePrintsOf(b.m, b.box.doc, b.name);
    b.AddTab();
    const auto hit = kernel::FindAffectedSketches(b.box.doc, b.name, before, kernel::SourcePrintsOf(b.m, b.box.doc, b.name));
    REQUIRE(hit.size() == 1);

    SUBCASE("отрицательный зазор отвергнут, деталь не тронута") {
        const std::size_t lines = b.BoxSketch().sketch.lines.size();
        CHECK_FALSE(kernel::MimicSourceChange(b.m, b.a, b.boxId, b.box.doc, b.name, hit, -1.0));
        CHECK(b.BoxSketch().sketch.lines.size() == lines);
    }
    SUBCASE("зазор 0,2 мм") {
        const Expected<int> made = kernel::MimicSourceChange(b.m, b.a, b.boxId, b.box.doc, b.name, hit, 0.2);
        REQUIRE_MESSAGE(made, (made ? "" : made.GetError().message));
        CHECK(*made == 8);   // the plate's new contour: 8 sides with the tab
        doc::SketchFeature& sk = b.BoxSketch();
        const sketch::Sketch& s = sk.sketch;
        CHECK(sk.contextProjections.size() == 8);
        for (const doc::ContextProjection& p : sk.contextProjections) {
            CHECK(p.adaptive);
            CHECK(p.source.IsSet());
        }
        CHECK(sk.analysis.failing.empty());
        // The old outline is gone; the new one goes round the tab 0.2 away.
        for (ObjectId old : outline.lines) CHECK_FALSE(s.FindLine(old));
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        int own = 0;
        for (const sketch::Line& l : s.lines) {
            if (l.projected || l.construction) continue;
            ++own;
            for (ObjectId p : {l.start, l.end}) {
                const Vec2 v = s.PositionOf(p);
                x0 = std::min(x0, v.x), x1 = std::max(x1, v.x), y0 = std::min(y0, v.y), y1 = std::max(y1, v.y);
            }
        }
        CHECK(own == 8);
        CHECK(x0 == Approx(-0.2));
        CHECK(x1 == Approx(50.2));
        CHECK(y0 == Approx(-0.2));
        CHECK(y1 == Approx(40.2));
        // The hole stays, still 10 from the (new) left side.
        const sketch::Constraint* dim = s.FindConstraint(outline.holeDimension);
        REQUIRE(dim);
        CHECK(dim->refs[0] == outline.holeCentre);
        CHECK(s.FindLine(dim->refs[1]));
        CHECK(s.FindLine(dim->refs[1])->projected);
        CHECK(s.PositionOf(outline.holeCentre).x == Approx(10));
        // The box's source prints are those of the new contour: nothing affected any more.
        const kernel::SourcePrints after = kernel::SourcePrintsOf(b.m, b.box.doc, b.name);
        CHECK(kernel::FindAffectedSketches(b.box.doc, b.name, after, after).empty());
        b.box.Rebuild();
    }
}

// Not a check: the files of scripts/ui-test/scenario-change-dialog.sh, written
// where BZ_UI_FIXTURE_DIR says (skipped without it). «Короб» is a 3 mm wall
// round «Основание»; «Основание-выступ.bzc» is the plate with a tab, copied
// over «Основание.bzc» while the assembly is open.
TEST_CASE("фикстура UI-сценария окна изменений") {
    const char* where = std::getenv("BZ_UI_FIXTURE_DIR");
    if (!where) return;
    Bench b;
    DrawOutline(b, 3);
    test::AddExtrude(b.box.doc, b.BoxSketch().id, "15");
    b.box.Rebuild();
    REQUIRE(b.box.model.Bodies().size() == 1);
    const std::filesystem::path dir = where;
    std::filesystem::create_directories(dir);
    REQUIRE(doc::SaveBzc(b.plate.doc, dir / "Основание.bzc", "tests"));
    REQUIRE(doc::SaveBzc(b.box.doc, dir / "Короб.bzc", "tests"));
    REQUIRE(assembly::SaveAssembly(b.a, dir / "Узел.bza", "tests"));
    b.AddTab();
    REQUIRE(doc::SaveBzc(b.plate.doc, dir / "Основание-выступ.bzc", "tests"));
}
