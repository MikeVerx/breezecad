#include "core/Folders.h"
#include "core/PathText.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace bz;

TEST_CASE("B-20: walking a folder with an unreadable entry does not throw") {
    // Audit 2026-10-13: entry.is_directory() without an error_code throws when
    // the file system refuses (access denied, a symlink loop); the autosave and
    // beta-report folders are walked before the window opens.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8("bz_folders_Автосохранение");
    fs::remove_all(dir);
    fs::create_directories(dir / "сессия");
    std::ofstream(dir / "info.json") << "{}";
    std::error_code ec;
    fs::create_symlink("петля", dir / "петля", ec);   // points to itself: ELOOP on status()
    const bool haveLoop = !ec;

    std::vector<fs::directory_entry> entries;
    CHECK_NOTHROW(entries = FolderEntries(dir));
    CHECK(entries.size() == (haveLoop ? 3u : 2u));
    int folders = 0, files = 0;
    for (const auto& e : entries) {
        CHECK_NOTHROW((void)IsFolder(e));
        CHECK_NOTHROW((void)IsFile(e));
        folders += IsFolder(e);
        files += IsFile(e);
    }
    CHECK(folders == 1);
    CHECK(files == 1);
    if (haveLoop) {
        CHECK_THROWS(fs::directory_entry(dir / "петля").is_directory());   // what the old code did
    }

    CHECK(FolderEntries(dir / "нет такой").empty());
    fs::remove_all(dir);
}
