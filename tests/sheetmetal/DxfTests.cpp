#include "sheetmetal/DxfDrawing.h"

#include <doctest.h>

#include <cmath>
#include <set>
#include <string>

using namespace bz;
using namespace bz::dxf;

namespace {

std::size_t Count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
    return n;
}

Drawing Sample() {
    Drawing d;
    d.EnsureLayer(kCut, 7);
    d.EnsureLayer(kBend, 1);
    d.entities.push_back(Entity::Line({0, 0}, {100, 0}, kCut));
    d.entities.push_back(Entity::Arc({100, 10}, 10, -90, 90, kCut));
    d.entities.push_back(Entity::Line({100, 20}, {0, 20}, kCut));
    d.entities.push_back(Entity::Line({0, 20}, {0, 0}, kCut));
    d.entities.push_back(Entity::Circle({50, 10}, 4, kCut));
    Entity poly;
    poly.kind = Kind::Polyline, poly.layer = kCut, poly.closed = true;
    poly.points = {{10, 5}, {20, 5}, {20, 15}};
    poly.bulges = {0, 1.0, 0};   // a half circle from (20,5) to (20,15)
    d.entities.push_back(poly);
    d.entities.push_back(Entity::Text({5, 30}, 3.5, 0, "Лист 2 мм, 90°", kBend));
    return d;
}

} // namespace

TEST_CASE("DXF R12 as AutoCAD lays it out: every table, handles, model space") {
    const std::string text = WriteR12(Sample());
    for (const char* table : {"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW", "UCS", "APPID", "DIMSTYLE"})
        CHECK(text.find(std::string("  2\n") + table + "\n") != std::string::npos);
    CHECK(text.find("Continuous") != std::string::npos);   // the line type the layers name is defined
    CHECK(text.find("$Model_Space") != std::string::npos);
    CHECK(text.find("$Paper_Space") != std::string::npos);
    CHECK(text.find("$HANDSEED") != std::string::npos);
    CHECK(text.find("ANSI_1252") != std::string::npos);
    // Plain ASCII: Cyrillic as \U+, degrees as %%d.
    for (const char c : text) CHECK(static_cast<unsigned char>(c) < 0x80);
    CHECK(text.find("\\U+041B\\U+0438\\U+0441\\U+0442 2 \\U+043C\\U+043C, 90%%d") != std::string::npos);
    // Handles are unique.
    std::set<std::string> handles;
    std::size_t total = 0;
    for (std::size_t at = text.find("\n  5\n"); at != std::string::npos; at = text.find("\n  5\n", at + 1)) {
        const std::size_t from = at + 5, to = text.find('\n', from);
        handles.insert(text.substr(from, to - from));
        ++total;
    }
    CHECK(handles.size() == total);
    CHECK(Count(text, "\nVERTEX\n") == 3);
    CHECK(Count(text, "\nSEQEND\n") == 1);
}

TEST_CASE("DXF written and read back is the same drawing") {
    const Drawing d = Sample();
    ReadReport report;
    const auto back = Read(WriteR12(d), &report);
    REQUIRE(back.HasValue());
    CHECK(report.version == "AC1009");
    REQUIRE(back->entities.size() == d.entities.size());
    CHECK(back->entities[1].kind == Kind::Arc);
    CHECK(back->entities[1].startDeg == doctest::Approx(-90));
    CHECK(back->entities[5].bulges[1] == doctest::Approx(1.0));
    CHECK(back->entities[5].closed);
    CHECK(back->entities[6].text == "Лист 2 мм, 90°");
    CHECK(back->entities[6].layer == kBend);
    REQUIRE(back->FindLayer(kBend));
    CHECK(back->FindLayer(kBend)->color == 1);
}

TEST_CASE("DXF from other programs: LWPOLYLINE, blocks, Windows-1251, inches, MTEXT") {
    const std::string text =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  9\n$DWGCODEPAGE\n  3\nANSI_1251\n  9\n$INSUNITS\n 70\n1\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nTABLES\n  0\nTABLE\n  2\nLAYER\n  0\nLAYER\n  2\nHIDDEN\n 70\n0\n 62\n-5\n  0\nENDTAB\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nBLOCKS\n  0\nBLOCK\n  2\nHOLE\n 10\n0\n 20\n0\n  0\nCIRCLE\n  8\n0\n 10\n0\n 20\n0\n 40\n0.25\n  0\nENDBLK\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLWPOLYLINE\n  8\nCUT\n 90\n4\n 70\n1\n 10\n0\n 20\n0\n 10\n4\n 20\n0\n 10\n4\n 20\n2\n 10\n0\n 20\n2\n"
        "  0\nINSERT\n  8\nHOLES\n  2\nHOLE\n 10\n1\n 20\n1\n 41\n2\n 42\n2\n"
        "  0\nTEXT\n  8\nMARK\n 10\n0\n 20\n3\n 40\n0.1\n  1\n\xC4\xE5\xF2\xE0\xEB\xFC\n"
        "  0\nMTEXT\n  8\nMARK\n 10\n0\n 20\n4\n 40\n0.1\n  1\n{\\fArial|b0;\\H0.2;\\U+0410\\PB}\n"
        "  0\nHATCH\n  8\n0\n"
        "  0\nENDSEC\n  0\nEOF\n";
    ReadReport report;
    const auto d = Read(text, &report);
    REQUIRE(d.HasValue());
    CHECK(report.unitsToMm == doctest::Approx(25.4));
    REQUIRE(d->entities.size() == 4);
    CHECK(d->entities[0].kind == Kind::Polyline);
    CHECK(d->entities[0].points[2].x == doctest::Approx(4 * 25.4));
    CHECK(d->entities[1].kind == Kind::Circle);   // the block, placed and scaled
    CHECK(d->entities[1].center.x == doctest::Approx(25.4));
    CHECK(d->entities[1].radius == doctest::Approx(0.5 * 25.4));
    CHECK(d->entities[1].layer == "HOLES");   // layer 0 inside a block: the insert's layer
    CHECK(d->entities[2].text == "Деталь");
    CHECK(d->entities[3].text == "А\nB");
    REQUIRE(report.skipped.size() == 1);
    CHECK(report.skipped[0].first == "HATCH");
    REQUIRE(d->FindLayer("HIDDEN"));
    CHECK_FALSE(d->FindLayer("HIDDEN")->visible);
    CHECK_FALSE(Read("not a dxf at all").HasValue());
}

TEST_CASE("checks before cutting: open ends, duplicates, closed contours") {
    Drawing d = Sample();
    Check c = CheckForCutting(d, kCut);
    CHECK(c.openEnds.empty());
    CHECK(c.duplicates.empty());
    CHECK(c.closedContours == 3);   // the outline, the circle, the polyline
    d.entities[2].a = {100, 20.5};   // a gap of 0.5 mm
    d.entities.push_back(Entity::Circle({50, 10}, 4, kCut));   // the same hole twice
    c = CheckForCutting(d, kCut);
    CHECK(c.openEnds.size() == 2);
    REQUIRE(c.duplicates.size() == 1);
    CHECK(c.duplicates[0] == d.entities.size() - 1);
    CHECK(c.closedContours == 3);   // circle + polyline + the duplicate circle
    // Within the tolerance the gap does not count.
    d.entities[2].a = {100, 20.005};
    CHECK(CheckForCutting(d, kCut, 0.01).openEnds.empty());
}

TEST_CASE("editing: distance, move, rotate, mirror, bounds") {
    Entity arc = Entity::Arc({0, 0}, 10, 0, 90, kCut);
    CHECK(Distance(arc, {20, 0}) == doctest::Approx(10));
    CHECK(Distance(arc, {0, -20}) == doctest::Approx(std::hypot(10, 20)));   // nearest end, not the circle
    Entity line = Entity::Line({0, 0}, {10, 0}, kCut);
    Move(line, {5, 5});
    CHECK(line.a.x == doctest::Approx(5));
    Rotate(line, {5, 5}, 90);
    CHECK(line.b.x == doctest::Approx(5));
    CHECK(line.b.y == doctest::Approx(15));
    Rotate(arc, {0, 0}, 90);
    CHECK(arc.startDeg == doctest::Approx(90));
    MirrorX(arc, 0);
    CHECK(arc.startDeg == doctest::Approx(0));
    CHECK(arc.endDeg == doctest::Approx(90));
    const Box2 box = Bounds(Sample());
    CHECK(box.max.x == doctest::Approx(110));
    CHECK(box.min.y == doctest::Approx(0).epsilon(1e-9));
    Entity poly = Sample().entities[5];
    CHECK(Distance(poly, {25, 10}) == doctest::Approx(0).epsilon(1e-9));   // on the half circle
}

TEST_CASE("text escapes both ways") {
    CHECK(EscapeText("Ø Лист") == "\\U+00D8 \\U+041B\\U+0438\\U+0441\\U+0442");
    CHECK(UnescapeText("\\U+041B%%d%%c%%p%%%") == "Л°⌀±%");
}

TEST_CASE("a spline given by its fit points passes through them") {
    const std::string text =
        "  0\nSECTION\n  2\nENTITIES\n  0\nSPLINE\n  8\n0\n 70\n8\n 71\n3\n 74\n3\n"
        " 11\n0\n 21\n0\n 11\n10\n 21\n10\n 11\n20\n 21\n0\n  0\nENDSEC\n  0\nEOF\n";
    const auto d = Read(text);
    REQUIRE(d.HasValue());
    REQUIRE(d->entities.size() == 1);
    const Entity& e = d->entities[0];
    CHECK(e.points.size() > 10);   // a curve, not three straight pieces
    CHECK(Distance(e, {10, 10}) == doctest::Approx(0).epsilon(1e-9));
    CHECK(e.points.back().x == doctest::Approx(20));
}

TEST_CASE("closing small gaps, not big ones, and not bending arcs") {
    Drawing d;
    d.entities.push_back(Entity::Line({0, 0}, {100, 0}, kCut));
    d.entities.push_back(Entity::Line({100.3, 0.2}, {100, 50}, kCut));   // 0.36 mm gap
    d.entities.push_back(Entity::Arc({50, 50}, 50, 0, 180, kCut));        // ends at (100,50) and (0,50)
    d.entities.push_back(Entity::Line({0, 52}, {0, 0}, kCut));            // 2 mm gap at (0,50)
    CHECK(CheckForCutting(d, kCut).openEnds.size() == 4);
    CHECK(CloseGaps(d, kCut, 0.5) == 1);
    CHECK(d.entities[0].b.x == doctest::Approx(100.15));
    CHECK(d.entities[1].a.x == doctest::Approx(100.15));
    CHECK(CheckForCutting(d, kCut).openEnds.size() == 2);
    CHECK(CloseGaps(d, kCut, 3) == 1);   // the 2 mm one: the line goes to the arc's end
    CHECK(d.entities[3].a.y == doctest::Approx(50));
    CHECK(d.entities[2].startDeg == 0);
    const Check c = CheckForCutting(d, kCut);
    CHECK(c.openEnds.empty());
    CHECK(c.closedContours == 1);
}

TEST_CASE("a text of several lines is written as one TEXT per line") {
    Drawing d;
    d.entities.push_back(Entity::Text({0, 10}, 5, 0, "Кронштейн\nS = 2 мм\tx", kMark));
    const std::string text = WriteR12(d);
    CHECK(Count(text, "\nTEXT\n") == 2);
    const auto back = Read(text);
    REQUIRE(back.HasValue());   // the file stays readable
    REQUIRE(back->entities.size() == 2);
    CHECK(back->entities[1].text == "S = 2 мм x");
    CHECK(back->entities[1].at.y == doctest::Approx(10 - 8));
}

TEST_CASE("numbers as DXF writers put them") {
    const std::string text = "  0\nSECTION\n  2\nENTITIES\n  0\nLINE\n  8\n0\n 10\n-12.5\n 20\n1.0E+02\n 11\n+3\n 21\n  1,25  \n  0\nENDSEC\n  0\nEOF\n";
    const auto d = Read(text);
    REQUIRE(d.HasValue());
    REQUIRE(d->entities.size() == 1);
    CHECK(d->entities[0].a.x == doctest::Approx(-12.5));
    CHECK(d->entities[0].a.y == doctest::Approx(100));
    CHECK(d->entities[0].b.x == doctest::Approx(3));
    CHECK(d->entities[0].b.y == doctest::Approx(1.25));
}
