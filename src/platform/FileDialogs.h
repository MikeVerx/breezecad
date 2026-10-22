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

/// Where a document asked to be saved at `file` really goes: on Android a
/// projects-folder document opened by 0.2.3.x has its copy elsewhere in the
/// cache — moved beside the others (so an assembly finds its parts by name).
/// The desktop: `file` itself.
std::filesystem::path SettledDocument(const std::filesystem::path& file);

/// A new file beside `document`, named `name` or like it when that is taken
/// ("Вал - Узел (2).bzc"). Empty: nothing can be put beside it (Android: a
/// document outside the projects folder). Not created on the desktop: the
/// caller writes it, then calls FileWritten.
std::filesystem::path NewFileBeside(const std::filesystem::path& document, const std::string& name);

/// Whether a part in a folder under an assembly's goes with it (the desktop);
/// on Android only the projects folder itself is carried.
bool PartsInSubfolders();

/// Why nothing can be put beside `document` (NewFileBeside gives nothing), for the user; empty when it can.
std::string BesideProblem(const std::filesystem::path& document);

/// Before reading `file`: on Android a projects-folder document is fetched
/// again (it may have been changed there: synced from a computer). Nothing on the desktop.
void RefreshDocument(const std::filesystem::path& file);

} // namespace bz::platform
