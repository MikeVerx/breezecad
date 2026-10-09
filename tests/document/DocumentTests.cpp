#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/Commands.h"
#include "document/CoilFeature.h"
#include "document/SectionFeature.h"
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
