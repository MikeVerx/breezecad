// Saving a document, on every platform the tests run on (Windows, Linux,
// macOS, the Android emulator). 0.2.3.2: on Android a save never reached the
// projects folder, which kept an empty file; these are the file layer's
// guarantees the fix stands on.
#include "Fixtures.h"

#include "core/PathText.h"
#include "document/BzcFile.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace bz;

namespace {

std::filesystem::path Folder(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / PathFromUtf8(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

doc::Document Part(int rectangles) {
    doc::Document d;
    for (int i = 0; i < rectangles; ++i) {
        doc::SketchFeature& sk = test::AddSketch(d);
        test::AddRect(d, sk, {0.0 + i, 0.0}, {10.0 + i, 5.0});
        test::AddExtrude(d, sk.id, "3");
    }
    return d;
}

} // namespace

TEST_CASE("an empty file says it is empty, not that it is another format") {
    const auto dir = Folder("bz_tests_пустой");
    const auto path = dir / PathFromUtf8("Деталь1.bzc");
    std::ofstream(path, std::ios::binary).close();   // what the projects folder kept up to 0.2.3.1
    REQUIRE(std::filesystem::file_size(path) == 0);

    const Expected<doc::Document> loaded = doc::LoadBzc(path);
    REQUIRE_FALSE(loaded.HasValue());
    CHECK(loaded.GetError().message.find("пуст") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("a cut-off file is refused with an error") {
    const auto dir = Folder("bz_tests_обрезанный");
    const auto path = dir / "part.bzc";
    REQUIRE(doc::SaveBzc(Part(3), path, "tests").IsOk());
    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size / 2);
    CHECK_FALSE(doc::LoadBzc(path).HasValue());
    std::filesystem::remove_all(dir);
}

TEST_CASE("a smaller save over a bigger one leaves exactly the new document") {
    // A file not cut to the new length keeps the old tail: a ZIP is read from
    // its end, so such a file did not open (the Android writer must truncate).
    const auto dir = Folder("bz_tests_перезапись");
    const auto path = dir / PathFromUtf8("Деталь.bzc");
    const doc::Document big = Part(6), small = Part(1);
    REQUIRE(doc::SaveBzc(big, path, "tests").IsOk());
    const auto bigSize = std::filesystem::file_size(path);
    REQUIRE(doc::SaveBzc(small, path, "tests").IsOk());
    CHECK(std::filesystem::file_size(path) < bigSize);

    const Expected<doc::Document> loaded = doc::LoadBzc(path);
    REQUIRE(loaded.HasValue());
    CHECK(doc::DocumentToJson(*loaded) == doc::DocumentToJson(small));
    CHECK_FALSE(std::filesystem::exists(dir / PathFromUtf8("Деталь.bzc.saving")));
    std::filesystem::remove_all(dir);
}

TEST_CASE("saving into a missing folder fails and creates nothing") {
    const auto dir = Folder("bz_tests_нет_папки");
    const auto path = dir / "gone" / "part.bzc";
    const Status s = doc::SaveBzc(Part(1), path, "tests");
    REQUIRE_FALSE(s.IsOk());
    CHECK(s.GetError().message.find("Папки нет") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(dir / "gone"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("a failed save keeps the previous file whole") {
    const auto dir = Folder("bz_tests_прежний");
    const auto path = dir / "part.bzc";
    const doc::Document first = Part(2);
    REQUIRE(doc::SaveBzc(first, path, "tests").IsOk());
    // The temporary name taken by a folder: the new file cannot be written.
    std::filesystem::create_directories(dir / "part.bzc.saving");
    CHECK_FALSE(doc::SaveBzc(Part(4), path, "tests").IsOk());

    const Expected<doc::Document> loaded = doc::LoadBzc(path);
    REQUIRE(loaded.HasValue());
    CHECK(doc::DocumentToJson(*loaded) == doc::DocumentToJson(first));
    std::filesystem::remove_all(dir);
}

TEST_CASE("many saves in a row, each one opens") {
    const auto dir = Folder("bz_tests_подряд");
    const auto path = dir / PathFromUtf8("Кронштейн (1).bzc");
    for (int n = 1; n <= 8; ++n) {
        CAPTURE(n);
        const doc::Document d = Part(n % 4 + 1);
        REQUIRE(doc::SaveBzc(d, path, "tests").IsOk());
        const Expected<doc::Document> loaded = doc::LoadBzc(path);
        REQUIRE(loaded.HasValue());
        CHECK(doc::DocumentToJson(*loaded) == doc::DocumentToJson(d));
    }
    std::size_t files = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator(dir)) ++files;
    CHECK(files == 1);   // no temporary files left behind
    std::filesystem::remove_all(dir);
}
