// "Разрез вида" stays on in a sketch (it used to go off there: 0.2.5 bug list).
#include "app/EditMode.h"

#include <doctest.h>

using namespace bz::app;

TEST_CASE("the view section is shown in the model, in a sketch and while picking its plane") {
    CHECK(SectionApplies(EditMode::Model));
    CHECK(SectionApplies(EditMode::Sketch));
    CHECK(SectionApplies(EditMode::PickPlane));
}

TEST_CASE("every edit mode is decided on: a new mode must say whether the section shows in it") {
    // Walks the enum as far as it goes; a mode added after PickPlane and Sketch
    // fails here until it is thought about.
    int modes = 0;
    for (int m = 0; m < 16; ++m) {
        const auto mode = static_cast<EditMode>(m);
        if (mode != EditMode::Model && mode != EditMode::Sketch && mode != EditMode::PickPlane) break;
        ++modes;
    }
    CHECK(modes == 3);
}
