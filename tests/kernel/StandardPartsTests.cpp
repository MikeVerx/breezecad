#include "document/BzcFile.h"
#include "document/Recompute.h"
#include "document/StandardPacks.h"
#include "document/StandardParts.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <tuple>

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

TEST_CASE("pack recipes: rounded polygon corners, typed inputs, a saved part as a kind") {
    // An equal angle with ГОСТ 8509's radii: root R inside, toe r at the leg ends;
    // its area is t(2b - t) + 0.2146(R² - 2r²). And a sheet with a typed width.
    const std::string text = R"json({
      "format": "breezecad-standard-parts", "version": 1, "id": "test.rolled", "title": "Прокат",
      "kinds": [
        {"id": "test.angle", "group": "Тест", "variety": "Уголки", "title": "Уголок", "noun": "Уголок",
         "build": [
           {"sketch": {"plane": "XY", "shapes": [{"polygon": {
              "points": [[0, 0], ["b", 0], ["b", "t"], ["t", "t"], ["t", "b"], [0, "b"]],
              "radii":  [0, 0, "r", "R", "r", 0]}}]}},
           {"extrude": {"distance": "L"}}],
         "envelope": ["b", "b", "L"]},
        {"id": "test.sheet", "group": "Тест", "variety": "Листы", "title": "Лист", "noun": "Лист",
         "build": [{"sketch": {"plane": "XY", "shapes": [{"rect": {"w": "L", "h": "B", "x": "L/2", "y": "B/2"}}]}},
                   {"extrude": {"distance": "s"}}],
         "envelope": ["L", "B", "s"]}],
      "standards": [
        {"id": "test.angle.a", "title": "ТУ 3", "kind": "test.angle", "size": "{b}x{t}",
         "columns": ["b", "t", "R", "r"], "rows": [[50, 5, 5.5, 1.8], [100, 10, 12, 4]], "length": {"min": 1, "max": 12000, "default": 1000}},
        {"id": "test.sheet.a", "title": "ТУ 4", "kind": "test.sheet", "size": "{s}", "designation": "Лист {s}×{B}×{L}",
         "columns": ["s"], "rows": [[2], [3]], "length": {"min": 10, "max": 6000, "default": 2000},
         "inputs": [{"name": "B", "title": "Ширина, мм", "min": 10, "max": 2500, "default": 1250}]}]
    })json";
    ClearPacks();
    Expected<Pack> pack = ParsePack(text);
    REQUIRE(pack);
    // Written and read back, the pack is the same.
    Expected<Pack> again = ParsePack(PackToJson(*pack));
    REQUIRE(again);
    CHECK(PackToJson(*again) == PackToJson(*pack));
    REQUIRE(AddPack(std::move(*pack)));

    for (const auto& [size, b, t, R, r] : {std::tuple{"50x5", 50.0, 5.0, 5.5, 1.8}, std::tuple{"100x10", 100.0, 10.0, 12.0, 4.0}}) {
        CheckPart({"test.angle.a", size, 1000});
        Expected<Made> made = MakePart({"test.angle.a", size, 1000});
        REQUIRE(made);
        kernel::Model model;
        model.Rebuild(made->doc);
        const double area = t * (2 * b - t) + 0.2146 * (R * R - 2 * r * r);
        CHECK(model.Volume() == doctest::Approx(area * 1000).epsilon(0.001));
    }

    const StandardInfo* sheet = FindStandard("test.sheet.a");
    REQUIRE(sheet);
    REQUIRE(Inputs(*sheet).size() == 1);
    CHECK(Inputs(*sheet)[0].preset == 1250);
    Request q{"test.sheet.a", "3", 2000};
    q.inputs = {{"B", 1500}};
    CHECK(Designation(q) == "Лист 3×1500×2000");
    Expected<Made> plate = MakePart(q);
    REQUIRE(plate);
    CHECK(plate->envelope.y == doctest::Approx(1500));
    CheckPart(q);
    q.inputs = {{"B", 9000}};
    CHECK_FALSE(MakePart(q));   // wider than the range

    // A part saved as a kind: the angle just made, its variables b, t take rows.
    Expected<Made> angle = MakePart({"test.angle.a", "50x5", 300});
    REQUIRE(angle);
    Pack mine;
    mine.id = "test.mine";
    mine.title = "Мои";
    PackKind k;
    k.id = "test.mine.angle";
    k.group = "Мои изделия";
    k.variety = "Уголки";
    k.title = "Уголок мой";
    k.noun = "Уголок";
    k.document = doc::DocumentToJson(angle->doc);
    mine.kinds.push_back(k);
    PackStandard st;
    st.id = "test.mine.angle.a";
    st.title = "Мой";
    st.kind = k.id;
    st.size = "{b}x{t}";
    st.columns = {{"b", "", true}, {"t", "", true}};
    st.rows = {{50, 5}, {60, 6}};
    mine.standards.push_back(st);
    Expected<Pack> read = ParsePack(PackToJson(mine));
    REQUIRE(read);
    REQUIRE(AddPack(std::move(*read)));
    Expected<Made> mine60 = MakePart({"test.mine.angle.a", "60x6", 0});
    REQUIRE(mine60);
    kernel::Model model;
    model.Rebuild(mine60->doc);
    for (const auto& f : mine60->doc.Features()) {
        INFO(f->name << ": " << f->status.message);
        CHECK(f->status.state == FeatureState::Ok);
    }
    const double area = 6.0 * (2 * 60 - 6) + 0.2146 * (5.5 * 5.5 - 2 * 1.8 * 1.8);   // R, r stay the saved part's
    CHECK(model.Volume() == doctest::Approx(area * 300).epsilon(0.001));
    CHECK(mine60->doc.info.designation == "Уголок 60x6 Мой");
    ClearPacks();
}

TEST_CASE("the user's library: a part saved with its variables as a table") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bz-user-library";
    fs::remove_all(dir);
    const fs::path file = dir / std::string(kUserPackFile);
    // The part to save: a ГОСТ 30245 profile, its variables a, t, R, L.
    const fs::path shipped[] = {fs::path(BZ_SOURCE_DIR) / "assets" / "library"};
    REQUIRE(LoadPacks(shipped).problems.empty());
    Expected<Made> profile = MakePart({"gost30245.square", "40x40x2", 500});
    REQUIRE(profile);

    UserItem item;
    item.title = "Стойка";
    item.variables = {"a", "t", "R", "L"};
    item.rows = {{40, 2, 4, 500}, {60, 3, 6, 500}};
    Expected<std::string> id = AddUserItem(file, profile->doc, item);
    REQUIRE(id);
    CHECK(*id == "user.1");
    CHECK_FALSE(AddUserItem(file, profile->doc, UserItem{"Без переменных", "Мои изделия", "Свои", {}, {}}));
    CHECK_FALSE(AddUserItem(file, profile->doc, UserItem{"Нет такой", "Мои изделия", "Свои", {"zz"}, {}}));
    // A second item: the part as it is (one row), no length.
    UserItem one;
    one.title = "Стойка короткая";
    one.variables = {"a", "t"};
    REQUIRE(AddUserItem(file, profile->doc, one));

    const fs::path folders[] = {dir, fs::path(BZ_SOURCE_DIR) / "assets" / "library"};
    const PacksLoaded loaded = LoadPacks(folders);
    for (const std::string& p : loaded.problems) INFO(p);
    REQUIRE(loaded.problems.empty());
    const StandardInfo* mine = FindStandard("user.1");
    REQUIRE(mine);
    CHECK(InEveryFamily(*mine));
    CHECK(KindOf(mine->kind).title == "Стойка");
    REQUIRE(FreeLength(*mine));
    CHECK(FreeLength(*mine)->preset == 500);
    CHECK(Sizes(*mine) == std::vector<std::string>{"40\xC3\x97" "2\xC3\x97" "4", "60\xC3\x97" "3\xC3\x97" "6"});
    // Built from the saved tree with the row's values: as heavy as the
    // standard's 60×60×3 (5,19 kg/m).
    Expected<Made> made = MakePart({"user.1", "60x3x6", 1000});
    REQUIRE(made);
    CHECK(made->doc.info.designation == "Стойка 60\xC3\x97" "3\xC3\x97" "6, L=1000");
    kernel::Model model;
    model.Rebuild(made->doc);
    CHECK(model.Volume() * 7.85e-6 == doctest::Approx(5.19).epsilon(0.01));
    const StandardInfo* small = FindStandard("user.2");
    REQUIRE(small);
    CHECK(Sizes(*small) == std::vector<std::string>{"40\xC3\x97" "2"});
    CHECK_FALSE(FreeLength(*small));

    // Taking them out: the file goes with the last one.
    REQUIRE(RemoveUserItem(file, "user.1"));
    CHECK_FALSE(RemoveUserItem(file, "user.1"));
    REQUIRE(RemoveUserItem(file, "user.2"));
    CHECK_FALSE(fs::exists(file));
    ClearPacks();
    fs::remove_all(dir);
}
