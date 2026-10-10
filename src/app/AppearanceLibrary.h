#pragma once
// The user's own appearances ("Мои"), kept in the config folder, one JSON
// file each, and moved between computers as a ZIP pack (.zip with an
// appearances.json inside) or a single .json file.

#include "core/Expected.h"
#include "document/Appearance.h"

#include <filesystem>
#include <vector>

namespace bz::app {

class AppearanceLibrary {
public:
    explicit AppearanceLibrary(std::filesystem::path folder) : myFolder(std::move(folder)) {}

    /// Reads the folder again (other windows of the program may have added some).
    void Reload();
    const std::vector<doc::Appearance>& Items() const { return myItems; }
    const doc::Appearance* Find(std::string_view id) const;

    /// Adds or replaces (same id) and saves it. A new one without an id gets one.
    Expected<doc::Appearance> Save(doc::Appearance appearance);
    Status Remove(std::string_view id);

    /// Reads a pack (.zip) or a single appearance (.json) into the library.
    /// Returns how many were added.
    Expected<int> Import(const std::filesystem::path& file);
    /// Writes the given appearances as a pack (.zip).
    static Status Export(const std::vector<doc::Appearance>& items, const std::filesystem::path& file);

private:
    std::filesystem::path        myFolder;
    std::vector<doc::Appearance> myItems;
};

} // namespace bz::app
