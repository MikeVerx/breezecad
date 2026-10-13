#pragma once
// Walking folders without exceptions.
//
// std::filesystem's directory_iterator::operator++ and directory_entry::
// is_directory() / is_regular_file() without an error_code throw when the
// file system says no: access denied (antivirus, OneDrive placeholders), a
// symlink loop, a folder removed meanwhile. Uncaught at start-up that closed
// the program before its window (audit 2026-10-13, B-20).

#include <filesystem>
#include <system_error>
#include <vector>

namespace bz {

/// Entries of `dir`, as many as could be read; a missing or unreadable
/// folder gives none.
inline std::vector<std::filesystem::directory_entry> FolderEntries(const std::filesystem::path& dir) {
    std::vector<std::filesystem::directory_entry> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    while (!ec && it != end) {
        out.push_back(*it);
        it.increment(ec);
    }
    return out;
}

/// is_directory / is_regular_file that answer "no" instead of throwing.
inline bool IsFolder(const std::filesystem::directory_entry& e) {
    std::error_code ec;
    return e.is_directory(ec) && !ec;
}

inline bool IsFile(const std::filesystem::directory_entry& e) {
    std::error_code ec;
    return e.is_regular_file(ec) && !ec;
}

} // namespace bz
