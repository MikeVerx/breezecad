// Packs of standard parts: reading and checking the format (no geometry).

#include "document/StandardPacks.h"
#include "document/StandardParts.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace bz;
using namespace bz::doc::parts;

namespace {

/// A small valid pack; `patch` replaces one piece of text to break it.
std::string SleevePack(std::string_view from = {}, std::string_view to = {}) {
    std::string text = R"json({
  "format": "breezecad-standard-parts", "version": 1,
  "id": "test.sleeves", "title": "Втулки (тест)",
  "kinds": [{
    "id": "test.sleeve", "group": "Втулки", "variety": "Гладкие", "title": "Втулка гладкая", "noun": "Втулка",
    "size_title": "D×d, мм",
    "build": [
      {"sketch": {"plane": "XZ", "shapes": [{"polygon": {"points": [["d/2", 0], ["D/2", 0], ["D/2", "L"], ["d/2", "L"]]}},
                                            {"axis": {"ends": [[0, 0], [0, "L"]]}}]}},
      {"revolve": {}}
    ],
    "envelope": ["D", "D", "L"]
  }],
  "standards": [{
    "id": "test.sleeve.a", "title": "ТУ тест", "family": "ГОСТ", "kind": "test.sleeve",
    "size": "{D}×{d}",
    "columns": ["D", "d", {"name": "m", "title": "Масса", "length": false}],
    "rows": [[10, 6, 1], [12, 8, 1.5], [20, 12.5, 4]],
    "lengths": [10, 20, 30]
  }]
})json";
    if (!from.empty()) {
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
    }
    return text;
}

std::string ErrorOf(std::string_view text) {
    const Expected<Pack> pack = ParsePack(text);
    return pack ? std::string{} : pack.GetError().message;
}

} // namespace

TEST_CASE("a valid pack reads with its kinds, standards and table") {
    const Expected<Pack> pack = ParsePack(SleevePack());
    REQUIRE(pack);
    CHECK(pack->id == "test.sleeves");
    REQUIRE(pack->kinds.size() == 1);
    CHECK(pack->kinds[0].build.size() == 2);
    CHECK(pack->kinds[0].build[0].shapes[1].type == PackShape::Type::Axis);
    CHECK(pack->kinds[0].build[1].operation == PackStep::Operation::New);
    REQUIRE(pack->standards.size() == 1);
    const PackStandard& s = pack->standards[0];
    CHECK(s.columns.size() == 3);
    CHECK_FALSE(s.columns[2].length);
    CHECK(s.length.mode == PackLength::Mode::List);
    CHECK(PackSize(s, 2) == "20\xC3\x97" "12.5");
}

TEST_CASE("broken packs are refused with a reason") {
    CHECK(ErrorOf("not json").find("JSON") != std::string::npos);
    CHECK(ErrorOf(SleevePack("\"version\": 1", "\"version\": 99")).find("новее") != std::string::npos);
    CHECK(ErrorOf(SleevePack("breezecad-standard-parts", "something")).find("format") != std::string::npos);
    // An expression naming what the table does not have.
    CHECK(ErrorOf(SleevePack("[\"D/2\", \"L\"]", "[\"D2/2\", \"L\"]")).find("D2") != std::string::npos);
    // A row of the wrong length; a size twice.
    CHECK(ErrorOf(SleevePack("[12, 8, 1.5]", "[12, 8]")).find("строка") != std::string::npos);
    CHECK(ErrorOf(SleevePack("[12, 8, 1.5]", "[10, 6, 2]")).find("повторяется") != std::string::npos);
    // A size that makes the part impossible (inner bigger than outer is fine for
    // a polygon, but a non-positive envelope is not).
    CHECK(ErrorOf(SleevePack("[20, 12.5, 4]", "[0, 12.5, 4]")).find("больше нуля") != std::string::npos);
    // Column names that would clash with the document's own names.
    CHECK(ErrorOf(SleevePack("[\"D\", \"d\",", "[\"D\", \"d1\",")).find("d1") != std::string::npos);
    CHECK(ErrorOf(SleevePack("{\"revolve\": {}}", "{\"extrude\": {\"distance\": \"L\"}}")) == "");
    // A revolve needs an осевая in its sketch.
    CHECK(ErrorOf(SleevePack(",\n                                            {\"axis\": {\"ends\": [[0, 0], [0, \"L\"]]}}", "")).find("осевой") !=
          std::string::npos);
    // An operation before any sketch; a recipe with no solid.
    CHECK(ErrorOf(SleevePack("{\"sketch\":", "{\"revolve\": {}}, {\"sketch\":")).find("до первого эскиза") != std::string::npos);
    CHECK(ErrorOf(SleevePack(",\n      {\"revolve\": {}}", "")).find("не создаёт тела") != std::string::npos);
    CHECK(ErrorOf(SleevePack("\"family\": \"ГОСТ\"", "\"family\": \"ANSI\"")).find("family") != std::string::npos);
}

TEST_CASE("loaded packs join the library") {
    ClearPacks();
    Expected<Pack> pack = ParsePack(SleevePack());
    REQUIRE(pack);
    REQUIRE(AddPack(std::move(*pack)));
    const StandardInfo* s = FindStandard("test.sleeve.a");
    REQUIRE(s);
    CHECK(IsPackKind(s->kind));
    CHECK(KindOf(s->kind).title == "Втулка гладкая");
    CHECK(FamilyOf(*s) == Family::Gost);
    CHECK(SizeTitle(*s) == "D×d, мм");
    CHECK(Sizes(*s).size() == 3);
    CHECK(Lengths(*s, "10\xC3\x97" "6") == std::vector<double>{10, 20, 30});
    CHECK_FALSE(FreeLength(*s));
    // Typed sizes: Latin x, a comma.
    CHECK(MatchSize(*s, "20x12,5") == "20\xC3\x97" "12.5");
    // A new folder after the built-in ones.
    bool found = false;
    for (const GroupInfo& g : Groups())
        if (g.name == "Втулки") found = g.kinds.size() == 1 && g.kinds[0] == s->kind;
    CHECK(found);
    // The default designation, with a ГОСТ comma.
    CHECK(Designation({"test.sleeve.a", "20x12.5", 30}) == "Втулка 20\xC3\x97" "12,5\xC3\x97" "30 ТУ тест");
    // The same pack (or one with the same standard) is not added twice.
    CHECK_FALSE(AddPack(*ParsePack(SleevePack())));
    // A standards-only pack for a kind of another pack is checked against it.
    const std::string extra = R"json({"format": "breezecad-standard-parts", "version": 1, "id": "test.more", "title": "Ещё",
      "standards": [{"id": "test.sleeve.b", "title": "DIN 0", "kind": "test.sleeve", "size": "{D}/{d}",
                     "columns": ["D", "d"], "rows": [[30, 20]], "lengths": [5]}]})json";
    Expected<Pack> more = ParsePack(extra);
    REQUIRE(more);
    REQUIRE(AddPack(std::move(*more)));
    s = FindStandard("test.sleeve.a");   // adding a pack rebuilds the catalogue: old pointers are gone
    REQUIRE(s);
    const StandardInfo* b = FindStandard("test.sleeve.b");
    REQUIRE(b);
    CHECK(b->kind == s->kind);
    CHECK(FamilyOf(*b) == Family::Din);
    CHECK(Analogues(*s).size() == 1);
    ClearPacks();
    CHECK_FALSE(FindStandard("test.sleeve.a"));
}

TEST_CASE("packs load from folders and install by copying") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "bz-pack-test";
    fs::remove_all(dir);
    fs::create_directories(dir / "in");
    std::ofstream(dir / "in" / "sleeves.bzlib") << SleevePack();
    std::ofstream(dir / "in" / "broken.bzlib") << "{";
    std::ofstream(dir / "in" / "ignored.json") << "{}";
    const Expected<fs::path> copy = InstallPack(dir / "in" / "sleeves.bzlib", dir / "library");
    REQUIRE(copy);
    CHECK(copy->filename() == "test.sleeves.bzlib");
    CHECK_FALSE(InstallPack(dir / "in" / "broken.bzlib", dir / "library"));
    const fs::path folders[] = {dir / "library", dir / "in"};
    const PacksLoaded loaded = LoadPacks(folders);
    CHECK(loaded.packs == 1);   // the copy; the original has the same id and is skipped quietly
    REQUIRE(loaded.problems.size() == 1);
    CHECK(loaded.problems[0].find("broken.bzlib") != std::string::npos);
    CHECK(FindStandard("test.sleeve.a"));
    ClearPacks();
    fs::remove_all(dir);
}

TEST_CASE("the shipped packs read and pass their checks") {
    const std::filesystem::path folders[] = {std::filesystem::path(BZ_SOURCE_DIR) / "assets" / "library"};
    const PacksLoaded loaded = LoadPacks(folders);
    for (const std::string& p : loaded.problems) INFO(p);
    CHECK(loaded.problems.empty());
    CHECK(loaded.packs >= 6);   // profiles, tubes, sheets, angles, channels, beams
    const StandardInfo* square = FindStandard("gost30245.square");
    REQUIRE(square);
    CHECK(MatchSize(*square, "40x40x2") == "40\xC3\x97" "40\xC3\x97" "2");
    const std::optional<LengthRange> range = FreeLength(*square);
    REQUIRE(range);
    CHECK(range->preset == 1000);
    CHECK(Designation({"gost30245.rect", "200x100x5.5", 1500}) == "Профиль 200\xC3\x97" "100\xC3\x97" "5,5 ГОСТ 30245-2003, L=1500");
    ClearPacks();
}
