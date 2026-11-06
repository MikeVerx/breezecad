// «Обнаружено изменение, затрагивающее другие детали» in the real
// AssemblyState: when a change of a source sketch is found, that the part
// waits for a choice instead of following quietly, and what each choice does.
#include "../Fixtures.h"
#include "app/AssemblyState.h"
#include "app/Session.h"
#include "document/EdgeFeature.h"
#include "document/Recompute.h"
#include "kernel/ContextProjection.h"

#include <doctest.h>
#include <imgui.h>

#include <cmath>
#include <filesystem>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;
using kernel::ChangeChoice;

namespace {

struct ImGuiContextForTests {
    ImGuiContextForTests() { ImGui::CreateContext(); }
    ~ImGuiContextForTests() { ImGui::DestroyContext(); }
};

/// «Основание» (a 40 x 40 x 10 plate) and «Короб» on its top, whose sketch
/// projects the plate's four top edges; both edited in this assembly.
struct Scene {
    ImGuiContextForTests  imgui;
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz_change_queue";
    std::filesystem::path file = dir / "Узел.bza";
    const std::string     context = "Узел";
    app::AssemblyState    st;
    ObjectId              plate, box, right;

    app::Session& Add(const std::string& name, doc::Document doc, const Placement& at, ObjectId& id) {
        auto session = std::make_unique<app::Session>();
        session->path = dir / (name + ".bzc");
        session->doc = std::move(doc);
        Component& c = st.assembly.AddComponent(name + ".bzc", name, at);
        c.grounded = true;
        id = c.id;
        st.parts.push_back({session->path, std::move(session), true});
        app::Session& s = *st.parts.back().session;
        s.RebuildNow();
        st.geometry.UsePartInMemory(s.path, &s.doc, &s.model);
        return s;
    }

    Scene() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        doc::Document p;
        auto& sk = test::AddSketch(p);
        test::AddRect(p, sk, {0, 0}, {40, 40});
        test::AddExtrude(p, sk.id, "10");
        Add("Основание", std::move(p), {}, plate);
        doc::Document b;
        test::AddSketch(b);
        Add("Короб", std::move(b), {Rotation{}, {0, 0, 10}}, box);
        Frames();
        for (Vec3 at : {Vec3{20, 0, 10}, Vec3{40, 20, 10}, Vec3{20, 40, 10}, Vec3{0, 20, 10}}) {
            const ObjectId curve = Project(at);
            if (at.x == 40) right = curve;
        }
        Box().doc.MarkChanged(0);
        Frames();
        REQUIRE(st.changes.empty());
    }
    ~Scene() { std::filesystem::remove_all(dir); }

    app::Session& Plate() { return *st.EditOf(dir / "Основание.bzc")->session; }
    app::Session& Box() { return *st.EditOf(dir / "Короб.bzc")->session; }
    static doc::SketchFeature& SketchOf(app::Session& s) { return *dynamic_cast<doc::SketchFeature*>(s.doc.Features()[0].get()); }

    void Frames() {
        for (int i = 0; i < 40; ++i) {
            bool changed = false;
            for (app::AssemblyState::PartEdit& e : st.parts)
                if (e.session) changed = e.session->RebuildNow() || changed;
            changed = st.Update(file, context) || changed;
            if (!changed && i > 2) break;
        }
    }

    ObjectId Project(Vec3 world) {
        auto& sk = SketchOf(Box());
        const std::optional<doc::EdgeReference> edge = st.geometry.EdgeNear(st.assembly, plate, world, 1.0);
        REQUIRE(edge);
        doc::ContextProjection link{context, plate, *edge, {}, true, {}};
        const Expected<kernel::ProjectedCurve> pc = st.geometry.ProjectSourceEdge(st.assembly, box, link, sk.placement.frame);
        REQUIRE(pc);
        IdGenerator& ids = Box().doc.Ids();
        const ObjectId a = sk.sketch.AddPoint(ids, pc->start), b = sk.sketch.AddPoint(ids, pc->end);
        sk.sketch.FindPoint(a)->projected = sk.sketch.FindPoint(b)->projected = true;
        link.curve = sk.sketch.AddLine(ids, a, b);
        sk.sketch.FindLine(link.curve)->projected = true;
        sk.contextProjections.push_back(link);
        return link.curve;
    }

    /// The highest y of the box's right line (40 before the tab, 15 after following it).
    double RightTop() {
        const sketch::Sketch& s = SketchOf(Box()).sketch;
        const sketch::Line* l = s.FindLine(right);
        REQUIRE(l);
        return std::max(s.PositionOf(l->start).y, s.PositionOf(l->end).y);
    }

    /// A 10 x 10 tab on the plate's right side, drawn in its sketch (as an edit in place does).
    void AddTab() {
        sketch::Sketch& s = SketchOf(Plate()).sketch;
        IdGenerator& ids = Plate().doc.Ids();
        ObjectId top;
        for (const sketch::Point& p : s.points)
            if (std::abs(p.pos.x - 40) < 1e-9 && std::abs(p.pos.y - 40) < 1e-9) top = p.id;
        REQUIRE(top);
        const ObjectId cut = s.AddPoint(ids, {40, 15});
        for (sketch::Line& l : s.lines) {
            const bool vertical = std::abs(s.PositionOf(l.start).x - 40) < 1e-9 && std::abs(s.PositionOf(l.end).x - 40) < 1e-9;
            if (!vertical) continue;
            if (l.start == top) l.start = cut;
            else if (l.end == top) l.end = cut;
        }
        const ObjectId a1 = s.AddPoint(ids, {50, 15}), a2 = s.AddPoint(ids, {50, 25}), a3 = s.AddPoint(ids, {40, 25});
        s.AddLine(ids, cut, a1);
        s.AddLine(ids, a1, a2);
        s.AddLine(ids, a2, a3);
        s.AddLine(ids, a3, top);
        Plate().doc.MarkChanged(0);
    }
};

} // namespace

TEST_CASE("изменение эскиза источника: деталь ждёт решения, а не следует молча; скругление — молча") {
    Scene s;
    SUBCASE("скругление ребра основания: окна нет") {
        const std::optional<doc::EdgeReference> edge = s.st.geometry.EdgeNear(s.st.assembly, s.plate, {40, 20, 10}, 1.0);
        REQUIRE(edge);
        auto f = std::make_unique<doc::FilletFeature>();
        f->radius.expression = "3";
        f->id = s.Plate().doc.Ids().Next();
        f->name = "Скругление1";
        f->edges = {*edge};
        s.Plate().doc.Insert(std::move(f), s.Plate().doc.FeatureCount());
        s.Plate().doc.MarkChanged(1);
        s.Frames();
        CHECK(s.st.changes.empty());
        CHECK(s.RightTop() == Approx(40));
    }
    SUBCASE("выступ, пока основание правится на месте: решение ждёт конца правки") {
        s.st.inPlace = s.plate;
        s.AddTab();
        s.Frames();
        CHECK(s.st.changes.empty());
        CHECK(s.RightTop() == Approx(40));   // and the box does not follow meanwhile
        s.st.inPlace = {};
        s.Frames();
        REQUIRE(s.st.changes.size() == 1);
        const app::AssemblyState::SourceChange& ch = s.st.changes[0];
        CHECK(ch.component == s.box);
        CHECK(ch.part == "Короб");
        CHECK(ch.source == "Основание");
        CHECK(ch.sourceSketch == Scene::SketchOf(s.Plate()).name);
        REQUIRE(ch.sketches.size() == 1);
        CHECK(ch.sketches[0].curves == std::vector<ObjectId>{s.right});
        s.Frames();
        CHECK(s.st.changes.size() == 1);   // found once
        CHECK(s.RightTop() == Approx(40));
    }
}

TEST_CASE("решения окна: адаптировать, игнорировать, повторить контур, вручную; одна отмена") {
    Scene s;
    s.AddTab();
    s.Frames();
    REQUIRE(s.st.changes.size() == 1);
    const std::size_t undo = s.st.CanUndo() ? 1 : 0;
    (void)undo;

    SUBCASE("превью ничего не меняет") {
        const Expected<doc::Document> after = s.st.TryChange(0, ChangeChoice::Adapt, 0.2, s.context);
        REQUIRE(after);
        CHECK(s.RightTop() == Approx(40));
        CHECK(s.st.changes.size() == 1);
    }
    SUBCASE("автоадаптация, затем отмена") {
        CHECK(s.st.ResolveChange(0, ChangeChoice::Adapt, 0, true, s.file, s.context).empty());
        CHECK(s.st.changes.empty());
        CHECK(s.RightTop() == Approx(15));
        CHECK(s.st.UndoLabel().find("Автоматическое адаптирование") != std::string::npos);
        s.Frames();
        CHECK(s.st.changes.empty());
        REQUIRE(s.st.Undo());
        CHECK(s.RightTop() == Approx(40));
    }
    SUBCASE("игнорировать: остаётся как есть и больше не спрашивает") {
        CHECK(s.st.ResolveChange(0, ChangeChoice::Ignore, 0, true, s.file, s.context).empty());
        s.Frames();
        CHECK(s.st.changes.empty());
        CHECK(s.RightTop() == Approx(40));
        CHECK_FALSE(Scene::SketchOf(s.Box()).contextProjections[1].adaptive);
    }
    SUBCASE("мимикрирование с зазором") {
        CHECK(s.st.ResolveChange(0, ChangeChoice::Mimic, 0.2, true, s.file, s.context).empty());
        s.Frames();
        CHECK(s.st.changes.empty());
        CHECK(Scene::SketchOf(s.Box()).contextProjections.size() == 8);
        CHECK(s.Box().model.Bodies().empty());   // a sketch alone: nothing to build, nothing broken
    }
    SUBCASE("вручную: очередь эскизов") {
        CHECK(s.st.ResolveChange(0, ChangeChoice::Manual, 0, false, s.file, s.context).empty());
        CHECK(s.st.manual.component == s.box);
        CHECK(s.st.manual.sketches == std::vector<ObjectId>{Scene::SketchOf(s.Box()).id});
        CHECK_FALSE(s.st.manual.rollback);
    }
}
