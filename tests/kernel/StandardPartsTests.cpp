#include "document/StandardParts.h"
#include "kernel/Model.h"

#include <doctest.h>

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

TEST_CASE("unknown standard part requests are errors") {
    CHECK_FALSE(MakePart({"nope", "M10", 40}));
    CHECK_FALSE(MakePart({"iso4017", "M7", 40}));
    CHECK_FALSE(MakePart({"iso4017", "M10", 41}));
    CHECK(Lengths(*FindStandard("iso7089"), "10").empty());
}
