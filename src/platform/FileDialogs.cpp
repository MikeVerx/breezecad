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

std::filesystem::path SettledDocument(const std::filesystem::path& file) { return file; }

std::filesystem::path NewFileBeside(const std::filesystem::path& document, const std::string& name) {
    const std::filesystem::path folder = document.parent_path();
    const std::filesystem::path wanted = PathFromUtf8(name);
    std::error_code ec;
    if (!std::filesystem::exists(folder / wanted, ec)) return folder / wanted;
    const std::string stem = ToUtf8(wanted.stem()), ext = ToUtf8(wanted.extension());
    for (int n = 2;; ++n)
        if (const auto candidate = folder / PathFromUtf8(stem + " (" + std::to_string(n) + ")" + ext); !std::filesystem::exists(candidate, ec))
            return candidate;
}

bool PartsInSubfolders() { return true; }

std::string BesideProblem(const std::filesystem::path&) { return {}; }

void RefreshDocument(const std::filesystem::path&) {}

} // namespace bz::platform
