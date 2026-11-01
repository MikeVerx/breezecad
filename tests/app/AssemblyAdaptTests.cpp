// A part made in an assembly and drawn by its neighbours (adaptive
// projections), with constraints added around it: the real AssemblyState —
// its solving, its following and its undo — as the program runs it.
#include "../Fixtures.h"
#include "app/AssemblyState.h"
#include "app/Session.h"
#include "app/sketch/SketchRepair.h"
#include "document/BzcFile.h"
#include "document/Recompute.h"
#include "kernel/ContextProjection.h"

#include <doctest.h>
#include <imgui.h>

#include <filesystem>
#include <map>

using namespace bz;
using namespace bz::assembly;

namespace {

/// `w` x 40 x 10 plate from the origin up, with a hole of radius 4 at (20, 20).
doc::Document Plate(double w = 40) {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    test::AddRect(d, sk, {0, 0}, {w, 40});
    test::AddCircle(d, sk, {20, 20}, 4);
    test::AddExtrude(d, sk.id, "10");
    return d;
}

GeometryRef Face(ObjectId c, Vec3 at, Vec3 normal) { return {c, GeometryKind::Plane, at, normal, 0, at}; }

/// Session::Rebuild asks ImGui for the time: a context, as the window has.
struct ImGuiContextForTests {
    ImGuiContextForTests() { ImGui::CreateContext(); }
    ~ImGuiContextForTests() { ImGui::DestroyContext(); }
};

/// The base plate B (grounded), its neighbour N 20 mm to the right of it
/// (free), and the part P made in the assembly on B's top (grounded, as the
/// program makes it). P's sketch is a rectangle whose right side is N's left
/// top edge projected (it reaches up to N), with B's hole projected too.
struct Scene {
    ImGuiContextForTests  imgui;
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz_assembly_adapt";
    std::filesystem::path file = dir / "Узел.bza";
    const std::string     context = "Узел";
    app::AssemblyState    st;
    ObjectId              base, neighbour, part;

    Scene() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(doc::SaveBzc(Plate(), dir / "plate.bzc", "tests"));
        base = st.assembly.AddComponent("plate.bzc", "Основание").id;   // grounded, the first
        neighbour = st.assembly.AddComponent("plate.bzc", "Сосед", {Rotation{}, {60, 0, 0}}).id;
        Frames();

        // Made as CreatePartAt makes it: in memory, on B's top, grounded.
        auto session = std::make_unique<app::Session>();
        session->path = dir / "Деталь1.bzc";
        test::AddSketch(session->doc);
        Component& c = st.assembly.AddComponent("Деталь1.bzc", "Деталь1", {Rotation{}, {0, 0, 10}});
        c.grounded = true;
        part = c.id;
        st.parts.push_back({session->path, std::move(session), true});
        app::Session& p = Part();
        p.RebuildNow();
        st.geometry.UsePartInMemory(p.path, &p.doc, &p.model);
        Frames();

        // Its sketch, drawn on the neighbours as the sketch editor does.
        auto& sk = Sketch();
        IdGenerator& ids = p.doc.Ids();
        const auto [p0, p1] = ProjectLine({60, 20, 10});      // N's left top edge: (60, 0)-(60, 40)
        const ObjectId o0 = sk.sketch.AddPoint(ids, {0, 0}), o1 = sk.sketch.AddPoint(ids, {0, 40});
        sk.sketch.AddLine(ids, o0, p0);
        sk.sketch.AddLine(ids, p1, o1);
        sk.sketch.AddLine(ids, o1, o0);
        ProjectCircle({24, 20, 10});                           // B's hole: centre (20, 20), r 4
        test::AddExtrude(p.doc, sk.id, "10");
        p.doc.MarkChanged(0);
        p.RebuildNow();
        Frames();
        REQUIRE(st.assembly.FindComponent(part)->follower);
        REQUIRE(p.model.Bodies().size() == 1);
    }
    ~Scene() { std::filesystem::remove_all(dir); }

    app::Session& Part() { return *st.EditOf(dir / "Деталь1.bzc")->session; }
    doc::SketchFeature& Sketch() {
        auto* sk = dynamic_cast<doc::SketchFeature*>(Part().doc.Features()[0].get());
        REQUIRE(sk);
        return *sk;
    }

    /// The window's frames: the part edited rebuilt, the assembly updated,
    /// until nothing changes any more (at most 40).
    void Frames() {
        for (int i = 0; i < 40; ++i) {
            bool changed = false;
            for (app::AssemblyState::PartEdit& e : st.parts)
                if (e.session) changed = e.session->RebuildNow() || changed;
            changed = st.Update(file, context) || changed;
            if (!changed && i > 2) break;
        }
    }

    doc::ContextProjection Link(ObjectId source, Vec3 world) {
        const std::optional<doc::EdgeReference> edge = st.geometry.EdgeNear(st.assembly, source, world, 1.0);
        REQUIRE(edge);
        return {context, source, *edge, {}, true};
    }
    std::pair<ObjectId, ObjectId> ProjectLine(Vec3 world) {
        auto& sk = Sketch();
        doc::ContextProjection link = Link(neighbour, world);
        const Expected<kernel::ProjectedCurve> pc = st.geometry.ProjectSourceEdge(st.assembly, part, link, sk.placement.frame);
        REQUIRE(pc);
        REQUIRE(pc->kind == kernel::ProjectedCurve::Kind::Line);
        IdGenerator& ids = Part().doc.Ids();
        const ObjectId a = sk.sketch.AddPoint(ids, pc->start), b = sk.sketch.AddPoint(ids, pc->end);
        sk.sketch.FindPoint(a)->projected = sk.sketch.FindPoint(b)->projected = true;
        link.curve = sk.sketch.AddLine(ids, a, b);
        sk.sketch.FindLine(link.curve)->projected = true;
        sk.contextProjections.push_back(link);
        // Bottom point first, whichever way the edge runs.
        return pc->start.y < pc->end.y ? std::pair{a, b} : std::pair{b, a};
    }
    void ProjectCircle(Vec3 world) {
        auto& sk = Sketch();
        doc::ContextProjection link = Link(base, world);
        const Expected<kernel::ProjectedCurve> pc = st.geometry.ProjectSourceEdge(st.assembly, part, link, sk.placement.frame);
        REQUIRE(pc);
        REQUIRE(pc->kind == kernel::ProjectedCurve::Kind::Circle);
        IdGenerator& ids = Part().doc.Ids();
        const ObjectId c = sk.sketch.AddPoint(ids, pc->centre);
        sk.sketch.FindPoint(c)->projected = true;
        link.curve = sk.sketch.AddCircle(ids, c, pc->radius);
        sk.sketch.FindCircle(link.curve)->projected = true;
        sk.contextProjections.push_back(link);
    }

    /// Where everything is: the components, and every point of every sketch of P.
    struct State {
        std::map<ObjectId, Placement> placements;
        std::map<std::pair<ObjectId, ObjectId>, Vec2> points;
        std::map<std::pair<ObjectId, ObjectId>, double> radii;
    };
    State Now() {
        State s;
        for (const Component& c : st.assembly.components) s.placements[c.id] = c.placement;
        for (const auto& f : Part().doc.Features())
            if (const auto* sk = dynamic_cast<const doc::SketchFeature*>(f.get())) {
                for (const sketch::Point& p : sk->sketch.points) s.points[{f->id, p.id}] = p.pos;
                for (const sketch::Circle& c : sk->sketch.circles) s.radii[{f->id, c.id}] = c.radius;
            }
        return s;
    }
    /// The farthest anything moved from `was`, in mm (a turn: as the move of a point 100 mm off).
    static double Farthest(const State& was, const State& now) {
        double far = 0;
        for (const auto& [id, p] : was.placements) {
            const auto it = now.placements.find(id);
            if (it == now.placements.end()) return 1e9;
            far = std::max({far, (it->second.translation - p.translation).Length(),
                            100 * (it->second.rotation.col[0] - p.rotation.col[0]).Length(),
                            100 * (it->second.rotation.col[2] - p.rotation.col[2]).Length()});
        }
        for (const auto& [id, p] : was.points) {
            const auto it = now.points.find(id);
            if (it == now.points.end()) return 1e9;
            far = std::max(far, (it->second - p).Length());
        }
        for (const auto& [id, r] : was.radii) {
            const auto it = now.radii.find(id);
            if (it == now.radii.end()) return 1e9;
            far = std::max(far, std::abs(it->second - r));
        }
        return far;
    }

    ObjectId AddMate(GeometryRef a, GeometryRef b) {
        st.Checkpoint("Зависимость");   // as AssemblyUi does
        Constraint c;
        c.type = ConstraintType::Mate;
        c.first = a;
        c.second = b;
        return st.assembly.AddConstraint(c).id;
    }
};

} // namespace

TEST_CASE("адаптивность: деталь из сборки, зависимость и недостижимое совмещение второй гранью — ничего не разлетается") {
    // Миша: деталь, созданная в сборке, проецирует рёбра соседей; к ней
    // добавлена зависимость, потом совмещение второй гранью, которое вместе
    // с первым выполнить нельзя. Раньше все эскизы таких деталей разлетались.
    Scene s;
    const Scene::State start = s.Now();

    // The neighbour's left face against P's right face: holds already.
    const ObjectId first = s.AddMate(Face(s.neighbour, {0, 20, 5}, {-1, 0, 0}), Face(s.part, {60, 20, 5}, {1, 0, 0}));
    s.Frames();
    CHECK(s.st.assembly.FindConstraint(first)->state == ConstraintState::Satisfied);
    CHECK(Scene::Farthest(start, s.Now()) < 0.01);
    const Scene::State mated = s.Now();

    // The neighbour's right face against P's left face: the neighbour would
    // have to be on both sides of P at once.
    const ObjectId second = s.AddMate(Face(s.neighbour, {40, 20, 5}, {1, 0, 0}), Face(s.part, {0, 20, 5}, {-1, 0, 0}));
    s.Frames();
    const Scene::State after = s.Now();
    INFO("moved ", Scene::Farthest(mated, after), " mm");
    CHECK(Scene::Farthest(mated, after) < 0.01);
    // The impossible one is kept, shown as not holding; the first still holds.
    REQUIRE(s.st.assembly.FindConstraint(second));
    CHECK(s.st.assembly.FindConstraint(second)->state == ConstraintState::Failed);
    CHECK(s.st.assembly.FindConstraint(first)->state == ConstraintState::Satisfied);
    CHECK(s.Part().model.Bodies().size() == 1);

    // Ctrl+Z: the second constraint goes, everything as it was.
    REQUIRE(s.st.Undo());
    s.Frames();
    CHECK(s.st.assembly.FindConstraint(second) == nullptr);
    CHECK(Scene::Farthest(mated, s.Now()) < 1e-6);
    REQUIRE(s.st.Undo());
    s.Frames();
    CHECK(Scene::Farthest(start, s.Now()) < 1e-6);
}

TEST_CASE("адаптивность: сосед удалён — его проекция красная, деталь на месте; восстановление отвязывает, отмена в детали возвращает") {
    Scene s;
    const Scene::State start = s.Now();
    const ObjectId sketch = s.Sketch().id;
    ObjectId line;
    for (const doc::ContextProjection& p : s.Sketch().contextProjections)
        if (p.component == s.neighbour) line = p.curve;
    REQUIRE(line);

    s.st.Checkpoint("Удаление");
    s.st.assembly.RemoveComponent(s.neighbour);
    s.Frames();
    CHECK(s.Sketch().lostContextProjections.count(line) == 1);
    CHECK(s.Sketch().BrokenProjections().count(line) == 1);
    // Nothing of P moved: its curves stay where they were.
    Scene::State without = start;
    without.placements.erase(s.neighbour);
    CHECK(Scene::Farthest(without, s.Now()) < 1e-6);
    CHECK(s.Part().model.Bodies().size() == 1);

    // "Восстановить эскиз как до изменения": unlinked, as it stands.
    REQUIRE(app::RestoreSketchBeforeBreak(s.Part(), sketch));
    s.Frames();
    CHECK(s.Sketch().contextProjections.empty());
    CHECK_FALSE(s.Sketch().HasBrokenProjections());
    CHECK_FALSE(s.Sketch().sketch.FindLine(line)->projected);
    CHECK_FALSE(s.st.assembly.FindComponent(s.part)->follower);
    CHECK(Scene::Farthest(without, s.Now()) < 1e-6);

    // Ctrl+Z in the part: the links are back.
    s.Part().Undo();
    s.Frames();
    CHECK(s.Sketch().contextProjections.size() == 2);
    CHECK(s.Sketch().sketch.FindLine(line)->projected);
}

TEST_CASE("адаптивность: сосед сдвинут, размер на проекции не сходится — красная, восстановление возвращает эскиз до сдвига") {
    Scene s;
    auto& sk = s.Sketch();
    // The hole's centre (from B) to N's left edge (from N), 40 along X, dimensioned.
    ObjectId centre, bottom;
    for (const doc::ContextProjection& p : sk.contextProjections) {
        if (const sketch::Circle* c = sk.sketch.FindCircle(p.curve)) centre = c->center;
        if (const sketch::Line* l = sk.sketch.FindLine(p.curve)) bottom = l->start;
    }
    REQUIRE(centre);
    REQUIRE(bottom);
    const double dx = sk.sketch.PositionOf(bottom).x - sk.sketch.PositionOf(centre).x;
    REQUIRE(dx == doctest::Approx(40.0));
    sketch::Constraint d;
    d.type = sketch::ConstraintType::DistanceX;
    d.refs = {centre, bottom, {}};
    d.parameter = {"d91", "40", Quantity::Length, 40.0, {}};
    sk.sketch.AddConstraint(s.Part().doc.Ids(), d);
    s.Part().doc.MarkChanged(0);
    s.Part().RebuildNow();
    s.Frames();
    REQUIRE_FALSE(s.Sketch().HasBrokenProjections());
    const Vec2 bottomWas = s.Sketch().sketch.PositionOf(bottom);

    // N moved 5 mm away (as a drag leaves it): P follows, the 40 cannot hold.
    s.st.Checkpoint("Перемещение");
    s.st.assembly.FindComponent(s.neighbour)->placement.translation.x += 5;
    s.st.assembly.Touch();
    s.Frames();
    CHECK(s.Sketch().sketch.PositionOf(bottom).x == doctest::Approx(bottomWas.x + 5));
    CHECK(s.Sketch().HasBrokenProjections());
    REQUIRE(s.Sketch().beforeBreak);

    REQUIRE(app::RestoreSketchBeforeBreak(s.Part(), s.Sketch().id));
    s.Frames();
    CHECK((s.Sketch().sketch.PositionOf(bottom) - bottomWas).Length() < 1e-6);
    CHECK_FALSE(s.Sketch().HasBrokenProjections());
    CHECK(s.Sketch().contextProjections.empty());
    CHECK(s.Sketch().status.state == doc::FeatureState::Ok);
}
