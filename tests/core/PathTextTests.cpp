#include "core/PathText.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace bz;

TEST_CASE("B-04: SameFile with a file that is gone is false, not an exception") {
    // Audit 2026-10-13: an open document deleted on disk crashed the next Open
    // (std::filesystem::equivalent throws when a file is missing).
    const auto dir = std::filesystem::temp_directory_path() / PathFromUtf8("bz_same_file_Деталь");
    std::filesystem::create_directories(dir);
    const auto kept = dir / "B.bzc", gone = dir / "C.bzc";
    std::ofstream(kept) << "b";
    std::ofstream(gone) << "c";
    std::filesystem::remove(gone);

    CHECK_NOTHROW((void)SameFile(gone, kept));
    CHECK_FALSE(SameFile(gone, kept));
    CHECK_FALSE(SameFile(kept, gone));
    CHECK_FALSE(SameFile(gone, gone));
    CHECK(SameFile(kept, kept));
    CHECK(SameFile(kept, dir / ".." / dir.filename() / "B.bzc"));
    std::filesystem::remove_all(dir);
}
