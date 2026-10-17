#include "document/Recompute.h"
#include "document/StandardPacks.h"
#include "document/StandardParts.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace bz;
using namespace bz::doc::parts;
using doc::FeatureState;

namespace {

/// Builds `q`, checks every operation is fine, the volume and the envelope.
void CheckPart(const Request& q) {
    INFO(q.standard << " " << q.size << " x " << q.length);
    Expected<Made> made = MakePart(q);
    REQUIRE(made);
    kernel::Model model;
    model.Rebuild(made->doc);
    for (const auto& f : made->doc.Features()) {
        INFO(f->name << ": " << f->status.message);
        CHECK(f->status.state == FeatureState::Ok);
    }
    CHECK(model.Volume() > 0);
    const auto box = model.BoundingBox();
    REQUIRE(box);
    const Vec3 size = box->max - box->min;
    const Envelope& e = made->envelope;
    CHECK(size.x == doctest::Approx(e.x).epsilon(0.01));
    CHECK(size.y == doctest::Approx(e.y).epsilon(0.01));
    CHECK(size.z == doctest::Approx(e.z).epsilon(0.01));
    CHECK_FALSE(made->doc.info.designation.empty());
    CHECK(made->doc.info.material == "Сталь");
}

} // namespace

TEST_CASE("every standard part builds: smallest, middle and largest size") {
    for (const KindInfo& kind : Kinds())
        for (const StandardInfo* s : StandardsOf(kind.kind)) {
            const std::vector<std::string> sizes = Sizes(*s);
            REQUIRE_FALSE(sizes.empty());
            for (const std::size_t i : {std::size_t{0}, sizes.size() / 2, sizes.size() - 1}) {
                const std::vector<double> lengths = Lengths(*s, sizes[i]);
                CheckPart({std::string(s->id), sizes[i], lengths.empty() ? 0.0 : lengths[lengths.size() / 2]});
            }
        }
}

TEST_CASE("standard part sizes match the tables") {
    // ISO 4017 M10x40: s = 16, k = 6.4.
    Expected<Made> bolt = MakePart({"iso4017", "M10", 40});
    REQUIRE(bolt);
    CHECK(bolt->envelope.y == doctest::Approx(16));
    CHECK(bolt->envelope.z == doctest::Approx(46.4));
    CHECK(bolt->doc.info.designation == "Болт ISO 4017 \xE2\x80\x93 M10\xC3\x97" "40");
    // ГОСТ 7798 М12: s = 19.
    CHECK(Designation({"gost7798", "М12", 50}) == "Болт М12\xC3\x97" "50 ГОСТ 7798-70");
    Expected<Made> nut = MakePart({"gost5915", "М12", 0});
    REQUIRE(nut);
    CHECK(nut->envelope.y == doctest::Approx(19));
    // Bearing 6204 = 204: 20 x 47 x 14.
    const std::vector<std::string> gost = Sizes(*FindStandard("gost8338"));
    CHECK(std::ranges::find(gost, "204") != gost.end());
    Expected<Made> bearing = MakePart({"iso15", "6204", 0});
    REQUIRE(bearing);
    CHECK(bearing->envelope.x == doctest::Approx(47));
    CHECK(bearing->envelope.z == doctest::Approx(14));
}

TEST_CASE("a standard part stays parametric: the length variable drives the model") {
    Expected<Made> made = MakePart({"iso4762", "M8", 30});
    REQUIRE(made);
    kernel::Model model;
    model.Rebuild(made->doc);
    REQUIRE(model.BoundingBox());
    CHECK((model.BoundingBox()->max - model.BoundingBox()->min).z == doctest::Approx(38));
    for (Parameter& v : made->doc.variables)
        if (v.name == "L") v.expression = "50";
    made->doc.MarkChanged(0);
    model.Rebuild(made->doc);
    CHECK((model.BoundingBox()->max - model.BoundingBox()->min).z == doctest::Approx(58));
}

TEST_CASE("standard part catalogue: groups, analogues, matching sizes, look, real thread") {
    std::size_t kinds = 0;
    for (const GroupInfo& g : Groups()) kinds += g.kinds.size();
    CHECK(kinds == Kinds().size());
    const StandardInfo& gost = *FindStandard("gost8338");
    CHECK(MatchingSize(gost, "204", *FindStandard("iso15")) == "6204");
    CHECK(MatchingSize(*FindStandard("gost7798"), "М12", *FindStandard("iso4017")) == "M12");
    CHECK(Analogues(*FindStandard("gost3128")).size() == 2);
    CHECK(FamilyOf(*FindStandard("din127")) == Family::Din);
    CHECK(ShortName(*FindStandard("gost5915")) == "Гайка ГОСТ 5915-70");
    CHECK(Designation({"gost397", "3.2", 20}) == "Шплинт 3,2\xC3\x97" "20 ГОСТ 397-79");

    Expected<Made> bolt = MakePart({"gost7798", "М8", 40, true, "std.brass"});
    REQUIRE(bolt);
    REQUIRE(bolt->doc.look.bodies.size() == 1);
    CHECK(bolt->doc.look.bodies.front().appearance == "std.brass");
    kernel::Model model;
    model.Rebuild(bolt->doc);
    for (const auto& f : bolt->doc.Features()) CHECK(f->status.state == FeatureState::Ok);
    // A real thread takes material away: less than the plain shank.
    Expected<Made> plain = MakePart({"gost7798", "М8", 40});
    REQUIRE(plain);
    kernel::Model plainModel;
    plainModel.Rebuild(plain->doc);
    CHECK(model.Volume() < plainModel.Volume() - 10);
}

TEST_CASE("unknown standard part requests are errors") {
    CHECK_FALSE(MakePart({"nope", "M10", 40}));
    CHECK_FALSE(MakePart({"iso4017", "M7", 40}));
    CHECK_FALSE(MakePart({"iso4017", "M10", 41}));
    CHECK(Lengths(*FindStandard("iso7089"), "10").empty());
}

// --- Packs (StandardPacks.h): parts built from a recipe -----------------------


TEST_CASE("parts of the shipped packs build, with the mass of 1 m from the tables") {
    const std::filesystem::path folders[] = {std::filesystem::path(BZ_SOURCE_DIR) / "assets" / "library"};
    const PacksLoaded loaded = LoadPacks(folders);
    REQUIRE(loaded.problems.empty());
    for (const auto& pack : LoadedPacks())
        for (const PackStandard& ps : pack->standards) {
            const StandardInfo* s = FindStandard(ps.id);
            REQUIRE(s);
            const auto mass = std::ranges::find(ps.columns, std::string("m"), &PackColumn::name);
            for (const std::size_t row : {std::size_t{0}, ps.rows.size() / 3, ps.rows.size() / 2, ps.rows.size() - 1}) {
                const std::string size = Sizes(*s)[row];
                const std::vector<double> lengths = Lengths(*s, size);
                const double length = lengths.empty() ? 0.0 : lengths.front();
                CheckPart({ps.id, size, length});
                if (mass == ps.columns.end() || !FreeLength(*s)) continue;
                // Steel 7.85 g/cm³, as the standards count it: kg per metre.
                Expected<Made> made = MakePart({ps.id, size, 1000});
                REQUIRE(made);
                kernel::Model model;
                model.Rebuild(made->doc);
                const double table = ps.rows[row][static_cast<std::size_t>(mass - ps.columns.begin())];
                INFO(ps.id << " " << size);
                CHECK(model.Volume() * 7.85e-6 == doctest::Approx(table).epsilon(0.01));
            }
        }
    // A length outside the range is refused; none given takes the preset.
    CHECK_FALSE(MakePart({"gost30245.square", "40x40x2", 20000}));
    Expected<Made> preset = MakePart({"gost30245.square", "40x40x2", 0});
    REQUIRE(preset);
    CHECK(preset->envelope.z == doctest::Approx(1000));
    CHECK(preset->doc.info.name == "Профиль");
    // The table went into variables that drive the sketch: 40×40×2 made 40×40×3
    // (R = 6) by hand weighs as the table's 40×40×3, 3.30 kg/m.
    for (Parameter& v : preset->doc.variables) {
        if (v.name == "t") v.expression = "3";
        if (v.name == "R") v.expression = "6";
    }
    doc::RecomputeModel(preset->doc);
    kernel::Model edited;
    edited.Rebuild(preset->doc);
    CHECK(edited.Volume() * 7.85e-6 == doctest::Approx(3.30).epsilon(0.01));
    ClearPacks();
}

TEST_CASE("pack recipes: revolve about an осевая, offset circles, hexagons, cuts") {
    const std::string text = R"json({
      "format": "breezecad-standard-parts", "version": 1, "id": "test.shapes", "title": "Фигуры",
      "kinds": [
        {"id": "test.bush", "group": "Тест", "variety": "Втулки", "title": "Втулка", "noun": "Втулка",
         "build": [
           {"sketch": {"plane": "XZ", "shapes": [{"polygon": {"points": [["d/2", 0], ["D/2", 0], ["D/2", "L"], ["d/2", "L"]]}},
                                                 {"axis": {"ends": [[0, 0], [0, "L"]]}}]}},
           {"revolve": {}}],
         "envelope": ["D", "D", "L"]},
        {"id": "test.plate", "group": "Тест", "variety": "Пластины", "title": "Пластина", "noun": "Пластина",
         "build": [
           {"sketch": {"plane": "XY", "shapes": [{"rect": {"w": "A", "h": "B", "x": "-A/2 + A/2", "y": "B/4"}}]}},
           {"extrude": {"distance": "s", "direction": "symmetric"}},
           {"sketch": {"plane": "XY", "shapes": [{"circle": {"d": "c", "x": "-A/4", "y": "B/4"}},
                                                 {"circle": {"d": "c", "x": "A/4", "y": "B/4"}}]}},
           {"extrude": {"distance": "s", "direction": "symmetric", "operation": "cut"}},
           {"sketch": {"plane": "XY", "shapes": [{"hexagon": {"s": "c"}}]}},
           {"extrude": {"distance": "s"}}],
         "envelope": ["A", "B", "s"]}],
      "standards": [
        {"id": "test.bush.a", "title": "ТУ 1", "kind": "test.bush", "size": "{D}x{d}",
         "columns": ["D", "d"], "rows": [[20, 10], [30, 22]], "lengths": [15, 25]},
        {"id": "test.plate.a", "title": "ТУ 2", "kind": "test.plate", "size": "{A}x{B}",
         "columns": ["A", "B", "s", "c"], "rows": [[100, 60, 4, 10]], "length": {"min": 1, "max": 10, "default": 5}}]
    })json";
    ClearPacks();
    Expected<Pack> pack = ParsePack(text);
    REQUIRE(pack);
    REQUIRE(AddPack(std::move(*pack)));
    CheckPart({"test.bush.a", "30x22", 25});
    {
        Expected<Made> bush = MakePart({"test.bush.a", "20x10", 15});
        REQUIRE(bush);
        kernel::Model model;
        model.Rebuild(bush->doc);
        CHECK(model.Volume() == doctest::Approx(3.14159265 / 4 * (20 * 20 - 10 * 10) * 15).epsilon(0.001));
        // The table went into variables the user can change.
        bool hasD = false;
        for (const auto& v : bush->doc.variables) hasD = hasD || (v.name == "D" && v.value == 20);
        CHECK(hasD);
    }
    // The plate: a rectangle shifted up by B/4, two holes cut, a hexagon boss
    // (extruded up from the middle plane, so it stands s/2 above the plate).
    Expected<Made> plate = MakePart({"test.plate.a", "100x60", 0});
    REQUIRE(plate);
    kernel::Model model;
    model.Rebuild(plate->doc);
    for (const auto& f : plate->doc.Features()) {
        INFO(f->name << ": " << f->status.message);
        CHECK(f->status.state == FeatureState::Ok);
    }
    const double holes = 2 * 3.14159265 / 4 * 100 * 4, hexagon = 3 * std::sqrt(3.0) / 2 * (10 / std::sqrt(3.0)) * (10 / std::sqrt(3.0));
    CHECK(model.Volume() == doctest::Approx(100 * 60 * 4 - holes + hexagon * 4 / 2).epsilon(0.002));
    const auto box = model.BoundingBox();
    REQUIRE(box);
    CHECK(box->min.y == doctest::Approx(-15));
    CHECK(box->max.y == doctest::Approx(45));
    CHECK(box->max.z == doctest::Approx(4));
    ClearPacks();
}
