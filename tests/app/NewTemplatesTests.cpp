#include "app/NewTemplates.h"

#include <doctest.h>

using namespace bz;
using namespace bz::app;

TEST_CASE("the Создать window: folders and templates from the document kinds") {
    const auto groups = NewDocumentGroups(doc::DocumentTypeRegistry::Default());
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].name == "Детали");
    CHECK(groups[1].name == "Сборки");
    CHECK(groups[2].name == "Чертежи");
    CHECK(groups[0].Available());
    CHECK_FALSE(groups[2].Available());   // drawings: "позже"
    REQUIRE(groups[0].templates.size() == 2);
    CHECK(groups[0].templates[0].length == LengthUnit::Millimeter);
    CHECK(groups[0].templates[1].length == LengthUnit::Inch);
    CHECK(FindTemplate(groups, "assembly") != nullptr);
}

TEST_CASE("the Создать window starts on the last template, else a part in the default units") {
    const auto groups = NewDocumentGroups(doc::DocumentTypeRegistry::Default());
    CHECK(InitialTemplate(groups, "assembly", LengthUnit::Millimeter)->id == "assembly");
    CHECK(InitialTemplate(groups, "", LengthUnit::Inch)->id == "part.inch");
    CHECK(InitialTemplate(groups, "drawing", LengthUnit::Millimeter)->id == "part.mm");   // not available
    CHECK(InitialTemplate(groups, "gone.module", LengthUnit::Centimeter)->id == "part.mm");
}

TEST_CASE("a kind registered later (a module's) gets its own folder") {
    doc::DocumentTypeRegistry registry = doc::DocumentTypeRegistry::Default();
    registry.Register({.id = "sheetmetal", .title = "Листовая деталь", .untitledBase = "Лист", .group = "Детали", .icon = "tree-part"});
    registry.Register({.id = "cam", .title = "Обработка", .untitledBase = "ЧПУ", .group = "ЧПУ"});
    const auto groups = NewDocumentGroups(registry);
    REQUIRE(groups.size() == 4);
    CHECK(groups[0].templates.size() == 3);   // mm, inch, sheet metal
    CHECK(groups[3].name == "ЧПУ");
}
