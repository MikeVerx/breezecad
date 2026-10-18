#include "app/Settings.h"
#include "core/PathText.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace bz;
using namespace bz::app;

namespace {

std::filesystem::path WriteSettings(const std::string& text) {
    const auto dir = std::filesystem::temp_directory_path() / PathFromUtf8("bz_settings_Настройки");
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "settings.json.bak");
    std::ofstream(dir / "settings.json", std::ios::trunc) << text;
    return dir / "settings.json";
}

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("B-11: a settings field of another type gives the defaults, not a crash at start") {
    // Audit 2026-10-13: {"uiScale":"1,25"} closed the program before the window on every start.
    const char* samples[] = {
        R"({"uiScale":"1,25"})",
        R"({"theme":5})",
        R"({"mouse":{"orbit":["middle",1]}})",
        R"({"window":{"x":"left"}})",
    };
    for (const std::string text : samples) {
        CAPTURE(text);
        const auto file = WriteSettings(text);
        Settings s;
        CHECK_NOTHROW(s = Settings::Load(file));
        CHECK(s.uiScale == 0.0);
        CHECK_FALSE(s.loadProblem.empty());
        CHECK(ReadText(file.parent_path() / "settings.json.bak") == text);   // kept for the user
    }
}

TEST_CASE("B-11: a good settings file still loads, and loadProblem is not saved") {
    const auto file = WriteSettings(R"({"uiScale":1.25,"theme":"dark","language":"en","shortcuts":[1,2]})");
    Settings s = Settings::Load(file);
    CHECK(s.uiScale == doctest::Approx(1.25));
    CHECK(s.theme == ThemeChoice::Dark);
    CHECK(s.language == "en");
    CHECK(s.loadProblem.empty());
    CHECK_FALSE(std::filesystem::exists(file.parent_path() / "settings.json.bak"));

    s.loadProblem = "x";
    REQUIRE(s.Save(file));
    CHECK(ReadText(file).find("loadProblem") == std::string::npos);
    std::filesystem::remove_all(file.parent_path());
}

TEST_CASE("Mobile settings: saved and read back, out-of-range values clamped") {
    const auto file = WriteSettings("{}");
    Settings s = Settings::Load(file);
    CHECK(s.mobile.layout == 0);
    CHECK(s.mobile.leftWidth == 20);
    s.mobile.layout = 2;
    s.mobile.leftHanded = true;
    s.mobile.iconsOnly = true;
    s.mobile.rightWidth = 10;
    s.mobile.hotbar = {"view.section", "tools.measure"};
    REQUIRE(s.Save(file));
    const Settings back = Settings::Load(file);
    CHECK(back.mobile.layout == 2);
    CHECK(back.mobile.leftHanded);
    CHECK(back.mobile.iconsOnly);
    CHECK(back.mobile.rightWidth == 10);
    CHECK(back.mobile.hotbar == std::vector<std::string>{"view.section", "tools.measure"});

    const Settings wild = Settings::Load(WriteSettings(R"({"mobile":{"layout":9,"leftWidth":95,"labelScale":0}})"));
    CHECK(wild.mobile.layout == 2);
    CHECK(wild.mobile.leftWidth == 40);
    CHECK(wild.mobile.labelScale == doctest::Approx(0.6));
    std::filesystem::remove_all(file.parent_path());
}

TEST_CASE("Mobile settings: the gesture sets and sensitivity are saved") {
    using bz::app::mobile::FingerDrag;
    const auto file = WriteSettings("{}");
    Settings s = Settings::Load(file);
    // Defaults: with the cursor on two fingers move, three turn.
    CHECK(s.mobile.gesturesOn.drags.two == FingerDrag::Pan);
    CHECK(s.mobile.gesturesOn.drags.three == FingerDrag::Orbit);
    CHECK(s.mobile.gesturesOff.drags.one == FingerDrag::Orbit);
    s.mobile.gesturesOff.drags.two = FingerDrag::Zoom;
    s.mobile.gesturesOff.drags.pinch = false;
    s.mobile.gesturesOn.tap2 = bz::app::FingerTap::Fit;
    s.mobile.fingerPan = 1.75;
    REQUIRE(s.Save(file));
    const Settings back = Settings::Load(file);
    CHECK(back.mobile.gesturesOff.drags.two == FingerDrag::Zoom);
    CHECK_FALSE(back.mobile.gesturesOff.drags.pinch);
    CHECK(back.mobile.gesturesOn.tap2 == bz::app::FingerTap::Fit);
    CHECK(back.mobile.gesturesOn.drags.three == FingerDrag::Orbit);
    CHECK(back.mobile.fingerPan == doctest::Approx(1.75));
    std::filesystem::remove_all(file.parent_path());
}
