#include "core/I18n.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace bz;

namespace {

/// A folder with catalogs written for one test, removed afterwards.
struct Catalogs {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz-i18n-test";
    std::filesystem::path packs = dir / "packs";

    Catalogs() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(packs);
    }
    ~Catalogs() {
        (void)i18n::Use({"ru", "Русский", {}, true});
        std::filesystem::remove_all(dir);
    }
    void Write(const std::filesystem::path& file, const std::string& json) const { std::ofstream(file, std::ios::binary) << json; }
};

const i18n::Language* Find(const std::vector<i18n::Language>& all, std::string_view code) {
    for (const auto& l : all)
        if (l.code == code) return &l;
    return nullptr;
}

} // namespace

TEST_CASE("i18n: Russian is the source, a catalog translates exact strings") {
    Catalogs c;
    c.Write(c.dir / "en.json", R"({"code": "en", "name": "English", "strings": {
        "Готово": "Done", "Отмена": "", "Удалить {}": "Delete {}", "Выбрано рёбер: {}": "Edges chosen: {}",
        "Выдавливание": "Extrude", "Сломано: {}": "Broken: {:d}"}})");
    const auto all = i18n::Available(c.dir, c.packs);
    REQUIRE(all.size() == 2);
    CHECK(all[0].code == "ru");
    REQUIRE(Find(all, "en"));

    CHECK(std::string(Tr("Готово")) == "Готово");   // nothing loaded yet
    REQUIRE(i18n::Use(*Find(all, "en")));
    CHECK(i18n::Current() == "en");
    CHECK(std::string(Tr("Готово")) == "Done");
    CHECK(std::string(Tr("Отмена")) == "Отмена");   // empty translation: stays Russian
    CHECK(std::string(Tr("Нет такой строки")) == "Нет такой строки");
    CHECK(std::string(Tr("Готово##save")) == "Done##save");   // ImGui id kept
    CHECK(Trf("Выбрано рёбер: {}", 3) == "Edges chosen: 3");
    // Text made at run time: matched against keys with holes; holes translated too.
    CHECK(Tr(std::string("Удалить Выдавливание1")) == "Delete Выдавливание1");
    CHECK(Tr(std::string("Удалить Выдавливание")) == "Delete Extrude");
    // A broken placeholder in a translation falls back to the Russian pattern.
    CHECK(Trf("Сломано: {}", "x") == "Сломано: x");
}

TEST_CASE("i18n: a pack replaces the built-in catalog of its language and adds new ones") {
    Catalogs c;
    c.Write(c.dir / "en.json", R"({"code": "en", "name": "English", "strings": {"Готово": "Done"}})");
    c.Write(c.packs / "en-better.json", R"({"code": "en", "name": "English", "strings": {"Готово": "OK"}})");
    c.Write(c.packs / "de.json", R"({"code": "de", "name": "Deutsch", "strings": {"Готово": "Fertig"}})");
    c.Write(c.packs / "broken.json", "{ not json");
    c.Write(c.packs / "ru.json", R"({"code": "ru", "name": "Русский", "strings": {}})");
    const auto all = i18n::Available(c.dir, c.packs);
    CHECK(all.size() == 3);
    REQUIRE(Find(all, "en"));
    CHECK_FALSE(Find(all, "en")->builtIn);
    REQUIRE(i18n::Use(*Find(all, "en")));
    CHECK(std::string(Tr("Готово")) == "OK");
    REQUIRE(i18n::Use(*Find(all, "de")));
    CHECK(std::string(Tr("Готово")) == "Fertig");
}
