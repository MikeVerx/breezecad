#include "../Fixtures.h"
#include "document/Appearance.h"
#include "document/BzcFile.h"
#include "document/Commands.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;

TEST_CASE("colours as hex, with transparency") {
    CHECK(doc::ColorToHex(Color::Hex(0x1D4ED8)) == "#1D4ED8");
    CHECK(doc::ColorToHex(Color::Hex(0x1D4ED8, 0.5f)) == "#1D4ED880");
    const auto c = doc::ColorFromHex("#1D4ED880");
    REQUIRE(c);
    CHECK(c->a == doctest::Approx(128 / 255.0));
    CHECK_FALSE(doc::ColorFromHex("#12345"));
    CHECK_FALSE(doc::ColorFromHex("zzzzzz"));
}

TEST_CASE("the standard library has the metals, plastics and polycarbonate") {
    for (const char* id : {"std.steel", "std.aluminium", "std.blued", "std.castiron", "std.copper", "std.gold", "std.brass",
                           "std.silver", "std.pc.clear", "std.pc.smoked", "std.plastic.red", "std.plastic.white"})
        CHECK_MESSAGE(doc::FindStandard(id), id);
    CHECK(doc::FindStandard("std.pc.clear")->face.a < 1.0f);
}

TEST_CASE("a look: whole bodies, single faces, saved and undone") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {10, 10});
    auto& ex = AddExtrude(d, sk.id, "5");

    doc::Look look;
    look.SetBody(ex.id, doc::FindStandard("std.brass"));
    look.SetFace(ex.id, {.normal = {0, 0, 1}, .point = {5, 5, 5}}, doc::FindStandard("std.plastic.red"));
    CHECK(look.used.size() == 2);
    CHECK(look.OfBody(ex.id)->id == "std.brass");
    // The same face again replaces its colour; a whole body drops its faces.
    look.SetFace(ex.id, {.normal = {0, 0, 1}, .point = {5, 5, 5}}, doc::FindStandard("std.plastic.blue"));
    CHECK(look.faces.size() == 1);
    CHECK(look.faces[0].appearance == "std.plastic.blue");
    CHECK(look.used.size() == 2);   // red is no longer used

    doc::History history;
    const std::uint64_t before = d.Revision();
    history.Execute(d, doc::SetLook(look, "Внешний вид"));
    CHECK(d.Revision() > before);
    CHECK(d.DirtyFrom() == 0);   // untouched: a look needs no rebuild (still dirty from creation only)

    const auto loaded = doc::DocumentFromJson(doc::DocumentToJson(d));
    REQUIRE(loaded);
    CHECK(loaded->look == d.look);

    history.Undo(d);
    CHECK(d.look.Empty());

    look.SetBody(ex.id, nullptr);
    CHECK(look.Empty());
    CHECK(look.used.empty());
}
