#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/Commands.h"
#include "document/CoilFeature.h"
#include "document/SectionFeature.h"
#include "document/ShellFeature.h"
#include "document/WorkPointFeature.h"
#include "document/EdgeFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/HoleFeature.h"
#include "document/LoftFeature.h"
#include "document/Origin.h"
#include "document/PatternFeature.h"
#include "document/Recompute.h"
#include "document/RevolveFeature.h"
#include "document/SketchFeature.h"
#include "document/SweepFeature.h"
#include "document/WorkPlaneFeature.h"

#include <doctest.h>

#include <filesystem>

using namespace bz;
using namespace bz::doc;

namespace {

/// A document with a 60 x 40 rectangle sketch on XY and a 10 mm extrusion.
struct Plate {
    Document doc;
    ObjectId sketchId;
    ObjectId extrudeId;

    Plate() {
        auto sk = std::make_unique<SketchFeature>();
        sk->id = doc.Ids().Next();
        sk->name = doc.UniqueFeatureName("Эскиз");
        sk->placement = {origin::kPlaneXY, *origin::PlaneFrame(origin::kPlaneXY), false, {}};
        auto& s = sk->sketch;
        IdGenerator& ids = doc.Ids();
        const ObjectId o = s.EnsureOrigin(ids);
        ObjectId p[4] = {s.AddPoint(ids, {0, 0}), s.AddPoint(ids, {50, 0}), s.AddPoint(ids, {50, 30}),
                         s.AddPoint(ids, {0, 30})};
        ObjectId l[4];
        for (int i = 0; i < 4; ++i) l[i] = s.AddLine(ids, p[i], p[(i + 1) % 4]);
        using sketch::ConstraintType;
        const auto geo = [&](ConstraintType t, ObjectId a, ObjectId b = {}) {
            sketch::Constraint c;
            c.type = t;
            c.refs = {a, b};
            s.AddConstraint(ids, c);
        };
        geo(ConstraintType::Coincident, p[0], o);
        geo(ConstraintType::Horizontal, l[0]);
        geo(ConstraintType::Vertical, l[1]);
        geo(ConstraintType::Horizontal, l[2]);
        geo(ConstraintType::Vertical, l[3]);
        sketch::Constraint w;
        w.type = ConstraintType::Length;
        w.refs = {l[0], {}};
        w.parameter = {"d1", "width", Quantity::Length, 0, {}};
        s.AddConstraint(ids, w);
        sketch::Constraint h;
        h.type = ConstraintType::Length;
        h.refs = {l[1], {}};
        h.parameter = {"d2", "d1 * 2 / 3", Quantity::Length, 0, {}};
        s.AddConstraint(ids, h);
        sketchId = sk->id;
        doc.Insert(std::move(sk), 0);

        auto ex = std::make_unique<ExtrudeFeature>();
        ex->id = doc.Ids().Next();
        ex->name = doc.UniqueFeatureName("Выдавливание");
        ex->sketch = sketchId;
        ex->distance = {"d3", "10", Quantity::Length, 0, {}};
        extrudeId = ex->id;
        doc.Insert(std::move(ex), 1);

        doc.variables.push_back({"width", "60", Quantity::Length, 0, {}});
    }
};

} // namespace

TEST_CASE("parameters evaluate in dependency order") {
    Plate plate;
    CHECK(plate.doc.EvaluateParameters() == 0);
    const auto* sk = plate.doc.FindAs<SketchFeature>(plate.sketchId);
    CHECK(sk->sketch.constraints[5].parameter.value == doctest::Approx(60.0));
    CHECK(sk->sketch.constraints[6].parameter.value == doctest::Approx(40.0));
    CHECK(plate.doc.NextParameterName() == "d4");
}

TEST_CASE("cycles and bad expressions are reported per parameter") {
    Plate plate;
    plate.doc.variables.push_back({"a", "b + 1", Quantity::Length, 5, {}});
    plate.doc.variables.push_back({"b", "a + 1", Quantity::Length, 7, {}});
    plate.doc.variables.push_back({"c", "2 +", Quantity::Length, 9, {}});
    CHECK(plate.doc.EvaluateParameters() >= 3);
    CHECK(plate.doc.variables[3].HasError());
    CHECK(plate.doc.variables[3].value == doctest::Approx(9.0));   // keeps the last good value
    CHECK_FALSE(plate.doc.variables[0].HasError());
}

TEST_CASE("recompute solves sketches from expressions") {
    Plate plate;
    RecomputeModel(plate.doc);
    const auto* sk = plate.doc.FindAs<SketchFeature>(plate.sketchId);
    CHECK(sk->status.state == FeatureState::Ok);
    CHECK(sk->analysis.degreesOfFreedom == 0);
    CHECK(sk->sketch.PositionOf(sk->sketch.lines[1].end).x == doctest::Approx(60.0));
    CHECK(sk->sketch.PositionOf(sk->sketch.lines[1].end).y == doctest::Approx(40.0));
}

TEST_CASE("commands undo and redo, history tracks saved state") {
    Plate plate;
    History history;
    CHECK_FALSE(history.IsModified());

    auto edited = plate.doc.Find(plate.extrudeId)->Clone();
    static_cast<ExtrudeFeature&>(*edited).distance.expression = "25";
    history.Execute(plate.doc, ReplaceFeature(std::move(edited), "Выдавливание1"));
    CHECK(plate.doc.FindAs<ExtrudeFeature>(plate.extrudeId)->distance.expression == "25");
    CHECK(history.IsModified());

    history.Execute(plate.doc, RemoveFeature(plate.doc, plate.sketchId));
    CHECK(plate.doc.FeatureCount() == 1);
    CHECK(history.UndoLabel() == "Удалить Эскиз1");

    history.Undo(plate.doc);
    CHECK(plate.doc.FeatureCount() == 2);
    CHECK(plate.doc.IndexOf(plate.sketchId) == 0);
    history.Undo(plate.doc);
    CHECK(plate.doc.FindAs<ExtrudeFeature>(plate.extrudeId)->distance.expression == "10");
    CHECK_FALSE(history.IsModified());

    history.Redo(plate.doc);
    CHECK(plate.doc.FindAs<ExtrudeFeature>(plate.extrudeId)->distance.expression == "25");
    history.MarkSaved();
    CHECK_FALSE(history.IsModified());
}

TEST_CASE("history rolls back to a mark: a tool that failed half-way leaves no step behind") {
    Plate plate;
    History history;
    const auto edit = [&](const char* value) {
        auto edited = plate.doc.Find(plate.extrudeId)->Clone();
        static_cast<ExtrudeFeature&>(*edited).distance.expression = value;
        history.Execute(plate.doc, ReplaceFeature(std::move(edited), "Выдавливание1"));
    };
    edit("20");
    edit("30");
    history.Undo(plate.doc);   // "30" waits for redo
    const History::Mark mark = history.GetMark();

    // A new step after the mark (the redo tail goes): taken back, the
    // history as at the mark except the redo it lost.
    edit("40");
    history.RollBack(mark);
    CHECK(history.CanUndo());
    CHECK(history.UndoLabel() == "Выдавливание1");
    CHECK_FALSE(history.CanRedo());

    // Undo and redo since the mark: the position goes back, steps stay.
    edit("50");
    const History::Mark second = history.GetMark();
    history.Undo(plate.doc);
    history.Undo(plate.doc);
    history.RollBack(second);
    CHECK(history.GetMark() == second);

    // Steps before the mark replaced (undo, then a new step): the history no
    // longer fits the document put back, so it is cleared.
    history.Undo(plate.doc);
    history.Undo(plate.doc);
    edit("60");
    history.RollBack(second);
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.CanRedo());
    CHECK(history.IsModified());
}

TEST_CASE("end marker follows inserts and moves") {
    Plate plate;
    CHECK(plate.doc.EndMarker() == 2);
    History history;
    history.Execute(plate.doc, SetEndMarker(plate.doc, 1));
    CHECK_FALSE(plate.doc.IsActive(1));
    RecomputeModel(plate.doc);
    CHECK(plate.doc.Find(plate.extrudeId)->status.state == FeatureState::NotComputed);
    history.Undo(plate.doc);
    CHECK(plate.doc.EndMarker() == 2);
}

TEST_CASE("json round trip keeps the recipe") {
    Plate plate;
    plate.doc.units.length = LengthUnit::Inch;
    plate.doc.info = {"Плита", "БРЗ.001", "Сталь 20"};
    plate.doc.Find(plate.extrudeId)->suppressed = true;
    const std::string text = DocumentToJson(plate.doc);

    LoadReport report;
    Expected<Document> loaded = DocumentFromJson(text, &report);
    REQUIRE(loaded.HasValue());
    CHECK(report.warnings.empty());
    CHECK(loaded->units.length == LengthUnit::Inch);
    CHECK(loaded->info == PartInfo{"Плита", "БРЗ.001", "Сталь 20"});
    CHECK(loaded->FeatureCount() == 2);
    CHECK(loaded->variables.size() == 1);
    const auto* sk = loaded->FindAs<SketchFeature>(plate.sketchId);
    REQUIRE(sk);
    CHECK(sk->sketch.lines.size() == 4);
    CHECK(sk->sketch.constraints.size() == 7);
    CHECK(sk->sketch.constraints[6].parameter.expression == "d1 * 2 / 3");
    CHECK(sk->placement.frame.normal == Vec3{0, 0, 1});
    CHECK(loaded->Find(plate.extrudeId)->suppressed);
    CHECK(loaded->Ids().Last() >= plate.doc.Ids().Last());
    CHECK(DocumentToJson(*loaded) == text);
}

TEST_CASE("unknown feature types survive open and save") {
    const std::string text = R"({
        "units": {"length": "mm", "angle": "deg"},
        "features": [
            {"id": 40, "type": "sheetMetalFlange", "name": "Отбортовка1", "height": 12.5, "edges": [3, 4]}
        ]})";
    LoadReport report;
    Expected<Document> loaded = DocumentFromJson(text, &report);
    REQUIRE(loaded.HasValue());
    CHECK(report.warnings.size() == 1);
    CHECK(loaded->Features()[0]->status.state == FeatureState::Error);
    const std::string saved = DocumentToJson(*loaded);
    CHECK(saved.find("sheetMetalFlange") != std::string::npos);
    CHECK(saved.find("12.5") != std::string::npos);
    CHECK(loaded->Ids().Next().value > 40);
}

TEST_CASE("bzc file round trip and atomic save") {
    Plate plate;
    // Cyrillic folder and file name on purpose: through PathFromUtf8 they are
    // real Unicode names on Windows too, not text in the ANSI code page.
    const auto dir = std::filesystem::temp_directory_path() / PathFromUtf8("bz_tests_Документы");
    std::filesystem::create_directories(dir);
    const auto path = dir / PathFromUtf8("Плита.bzc");

    REQUIRE(SaveBzc(plate.doc, path, "BreezeCAD tests").IsOk());
    REQUIRE(SaveBzc(plate.doc, path, "BreezeCAD tests").IsOk());   // overwrite works
    CHECK(ToUtf8(path.filename()) == "Плита.bzc");
    CHECK_FALSE(std::filesystem::exists(dir / PathFromUtf8("Плита.bzc.saving")));

    Expected<Document> loaded = LoadBzc(path);
    REQUIRE(loaded.HasValue());
    CHECK(DocumentToJson(*loaded) == DocumentToJson(plate.doc));

    CHECK_FALSE(LoadBzc(dir / "missing.bzc").HasValue());
    std::filesystem::remove_all(dir);
}

TEST_CASE("every feature type survives a file round trip") {
    // Each operation with its default settings: saved, opened, same type and
    // no warnings. Catches a feature that writes over the common keys.
    doc::Document d;
    std::vector<std::unique_ptr<doc::Feature>> made;
    made.push_back(std::make_unique<doc::SketchFeature>());
    made.push_back(std::make_unique<doc::ExtrudeFeature>());
    made.push_back(std::make_unique<doc::RevolveFeature>());
    made.push_back(std::make_unique<doc::FilletFeature>());
    made.push_back(std::make_unique<doc::ChamferFeature>());
    made.push_back(std::make_unique<doc::HoleFeature>());
    made.push_back(std::make_unique<doc::WorkPlaneFeature>());
    made.push_back(std::make_unique<doc::PatternFeature>());
    made.push_back(std::make_unique<doc::MirrorFeature>());
    made.push_back(std::make_unique<doc::LoftFeature>());
    made.push_back(std::make_unique<doc::SweepFeature>());
    made.push_back(std::make_unique<doc::CoilFeature>());
    made.push_back(std::make_unique<doc::SectionFeature>());
    made.push_back(std::make_unique<doc::ShellFeature>());
    made.push_back(std::make_unique<doc::WorkPointFeature>());
    std::vector<std::pair<ObjectId, std::string_view>> expected;
    for (auto& f : made) {
        f->id = d.Ids().Next();
        f->name = d.UniqueFeatureName("Операция");
        expected.emplace_back(f->id, f->Type());
        d.Insert(std::move(f), d.FeatureCount());
    }
    const auto dir = std::filesystem::temp_directory_path() / "bz_all_types";
    std::filesystem::create_directories(dir);
    REQUIRE(doc::SaveBzc(d, dir / "all.bzc", "tests").IsOk());
    doc::LoadReport report;
    auto loaded = doc::LoadBzc(dir / "all.bzc", &report);
    std::filesystem::remove_all(dir);
    REQUIRE(loaded);
    for (const std::string& w : report.warnings) MESSAGE(w);
    CHECK(report.warnings.empty());
    for (const auto& [id, type] : expected) {
        const doc::Feature* f = loaded->Find(id);
        REQUIRE(f);
        CHECK(f->Type() == type);
    }
}

// Б4/Б5: «Скрыть» is not «Подавить»: it is saved, undone, and rebuilds nothing.
TEST_CASE("hiding a feature is saved, undone and needs no rebuild") {
    Plate plate;
    plate.doc.ClearDirty();
    const std::uint64_t revision = plate.doc.Revision();
    History history;
    history.Execute(plate.doc, SetHidden(plate.extrudeId, true, "Скрыть Выдавливание1"));
    CHECK(plate.doc.Find(plate.extrudeId)->hidden);
    CHECK_FALSE(plate.doc.Find(plate.extrudeId)->suppressed);
    CHECK(plate.doc.Revision() > revision);                     // the view and the saved state see it
    CHECK(plate.doc.DirtyFrom() == plate.doc.FeatureCount());   // nothing to rebuild
    CHECK(history.IsModified());

    // Saved only when set; older files without it open as shown.
    const std::string text = DocumentToJson(plate.doc);
    CHECK(text.find("\"hidden\"") != std::string::npos);
    Expected<Document> loaded = DocumentFromJson(text);
    REQUIRE(loaded.HasValue());
    CHECK(loaded->Find(plate.extrudeId)->hidden);
    CHECK_FALSE(loaded->Find(plate.sketchId)->hidden);

    history.Undo(plate.doc);
    CHECK_FALSE(plate.doc.Find(plate.extrudeId)->hidden);
    CHECK_FALSE(history.IsModified());
    CHECK(DocumentToJson(plate.doc).find("\"hidden\"") == std::string::npos);
    history.Redo(plate.doc);
    CHECK(plate.doc.Find(plate.extrudeId)->hidden);
}

TEST_CASE("renaming a variable renames it in every expression, undo puts it all back") {
    Plate plate;   // the sketch's width dimension is "width"
    plate.doc.variables.push_back({"half", "width / 2 + widths", Quantity::Length, 0, {}});
    plate.doc.variables.push_back({"widths", "1", Quantity::Length, 0, {}});
    auto* ex = plate.doc.FindAs<ExtrudeFeature>(plate.extrudeId);
    ex->distance.expression = "width/6 + sqrt(width*width)/60";
    REQUIRE(plate.doc.EvaluateParameters() == 0);
    const auto* sk = plate.doc.FindAs<SketchFeature>(plate.sketchId);
    const std::string sketchBefore = sk->sketch.constraints[5].parameter.expression;

    doc::History history;
    history.Execute(plate.doc, doc::RenameVariable(0, "w"));
    CHECK(plate.doc.variables[0].name == "w");
    CHECK(plate.doc.variables[1].expression == "w / 2 + widths");   // "widths" is another name
    CHECK(ex->distance.expression == "w/6 + sqrt(w*w)/60");
    CHECK(plate.doc.EvaluateParameters() == 0);
    CHECK(ex->distance.value == doctest::Approx(11.0));

    history.Undo(plate.doc);
    CHECK(plate.doc.variables[0].name == "width");
    CHECK(plate.doc.variables[1].expression == "width / 2 + widths");
    CHECK(ex->distance.expression == "width/6 + sqrt(width*width)/60");
    CHECK(sk->sketch.constraints[5].parameter.expression == sketchBefore);
    CHECK(plate.doc.EvaluateParameters() == 0);
}

TEST_CASE("a name that exists but cannot be used is not reported as unknown") {
    Plate plate;
    plate.doc.variables.push_back({"a", "a + 1", Quantity::Length, 0, {}});
    plate.doc.variables.push_back({"b", "c + 1", Quantity::Length, 0, {}});
    plate.doc.variables.push_back({"c", "b + 1", Quantity::Length, 0, {}});
    plate.doc.EvaluateParameters();
    CHECK(plate.doc.variables[1].error.find("сам на себя") != std::string::npos);
    for (const std::size_t i : {2u, 3u}) {
        CAPTURE(i);
        CHECK(plate.doc.variables[i].error.find("Циклическая") != std::string::npos);
    }
}

TEST_CASE("a deleted dimension still referenced does not come back under a new one") {
    Plate plate;   // d1, d2 in the sketch, d3 the extrusion
    auto* ex = plate.doc.FindAs<ExtrudeFeature>(plate.extrudeId);
    ex->distance.name = "d9";   // the largest...
    plate.doc.variables.push_back({"t", "d9 / 2", Quantity::Length, 0, {}});
    ex->distance.name = "";     // ...is gone, "t" still refers to it
    CHECK(plate.doc.NextParameterName() == "d10");
}

TEST_CASE("switching the document units keeps the part the same size, undo restores the texts") {
    for (const LengthUnit unit : {LengthUnit::Inch, LengthUnit::Centimeter, LengthUnit::Meter}) {
        CAPTURE(static_cast<int>(unit));
        Plate plate;   // width 60 (variable), d1 = width, d2 = d1 * 2 / 3, d3 = 10
        REQUIRE(plate.doc.EvaluateParameters() == 0);
        auto* ex = plate.doc.FindAs<ExtrudeFeature>(plate.extrudeId);
        const auto* sk = plate.doc.FindAs<SketchFeature>(plate.sketchId);
        const double depth = ex->distance.value, height = sk->sketch.constraints[6].parameter.value;
        UnitSystem other = plate.doc.units;
        other.length = unit;
        doc::History history;
        history.Execute(plate.doc, doc::SetUnits(other));
        REQUIRE(plate.doc.EvaluateParameters() == 0);
        CHECK(ex->distance.value == doctest::Approx(depth));
        CHECK(sk->sketch.constraints[6].parameter.value == doctest::Approx(height));
        CHECK(plate.doc.variables[0].value == doctest::Approx(60.0));
        CHECK(ex->distance.expression == "10mm");
        CHECK(sk->sketch.constraints[6].parameter.expression == "d1 * 2 / 3");   // nothing in it depends on units
        history.Undo(plate.doc);
        CHECK(ex->distance.expression == "10");
        CHECK(plate.doc.variables[0].expression == "60");
        REQUIRE(plate.doc.EvaluateParameters() == 0);
        CHECK(ex->distance.value == doctest::Approx(depth));
    }
}
