#include "platform/FileDialogs.h"
#include "core/Monitor.h"
#include "core/PathText.h"

#include <portable-file-dialogs.h>

namespace bz::platform {
namespace {

std::vector<std::string> Flatten(const std::vector<FileFilter>& filters) {
    std::vector<std::string> out;
    for (const auto& f : filters) {
        out.push_back(f.name + " (" + f.pattern + ")");
        out.push_back(f.pattern);
    }
    return out;
}

} // namespace

std::string FileDialogProblem() {
    if (pfd::settings::available()) return {};
    return "Не удалось показать окно выбора файла: установите программу zenity или kdialog";
}

std::optional<std::filesystem::path> ShowOpenDialog(const std::string& title, const std::vector<FileFilter>& filters,
                                                    const std::filesystem::path& folder) {
    const mon::Waiting waiting;   // the window waits for the dialog: not a hang
    const std::vector<std::string> result = pfd::open_file(title, ToUtf8(folder), Flatten(filters)).result();
    if (result.empty()) return std::nullopt;
    return PathFromUtf8(result.front());
}

std::optional<std::filesystem::path> ShowSaveDialog(const std::string& title, const std::filesystem::path& suggested,
                                                    const std::vector<FileFilter>& filters, const std::string& extension) {
    const mon::Waiting waiting;
    const std::string result = pfd::save_file(title, ToUtf8(suggested), Flatten(filters)).result();
    if (result.empty()) return std::nullopt;
    std::filesystem::path path = PathFromUtf8(result);
    // "Деталь.BZC" is a .bzc already (it became "Деталь.BZC.bzc").
    std::string typed = ToUtf8(path.extension()), wanted = extension;
    for (std::string* t : {&typed, &wanted})
        for (char& ch : *t)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    if (typed != wanted) path += PathFromUtf8(extension);
    return path;
}

} // namespace bz::platform
