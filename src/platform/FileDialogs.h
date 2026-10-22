#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bz::platform {

/// A filter like {"Документ BreezeCAD", "*.bzc"}.
struct FileFilter {
    std::string name;
    std::string pattern;
};

/// Empty when native file dialogs work. On Linux they need zenity or
/// kdialog; without them this returns a message for the user.
std::string FileDialogProblem();

/// Native "Open" dialog. nullopt when cancelled.
std::optional<std::filesystem::path> ShowOpenDialog(const std::string& title, const std::vector<FileFilter>& filters,
                                                    const std::filesystem::path& folder);

/// Native "Save as" dialog. Adds `extension` when the user did not type it.
std::optional<std::filesystem::path> ShowSaveDialog(const std::string& title, const std::filesystem::path& suggested,
                                                    const std::vector<FileFilter>& filters, const std::string& extension);

/// Must be called after the program wrote `file` (a saved document, an
/// export) to a path ShowSaveDialog or an import gave. On Android such a
/// path is a local copy standing for a document elsewhere (a picked or
/// projects-folder file): it is copied back there. False when that copy
/// failed — the file is NOT where the user expects it, say so. On the
/// desktop the file is already in its place: always true.
#if defined(__ANDROID__)
[[nodiscard]] bool FileWritten(const std::filesystem::path& file);
#else
[[nodiscard]] inline bool FileWritten(const std::filesystem::path&) { return true; }
#endif

} // namespace bz::platform
