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

/// Called after the program wrote `file` (a saved document, an export). On
/// Android a file standing for a document elsewhere (a picked or projects-
/// folder file) is copied back to it; nothing to do on the desktop.
#if defined(__ANDROID__)
void FileWritten(const std::filesystem::path& file);
#else
inline void FileWritten(const std::filesystem::path&) {}
#endif

} // namespace bz::platform
