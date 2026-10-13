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
