#include "platform/FileDialogs.h"

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

std::string Utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return {s.begin(), s.end()};
}

std::filesystem::path FromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

} // namespace

std::string FileDialogProblem() {
    if (pfd::settings::available()) return {};
    return "Не удалось показать окно выбора файла: установите программу zenity или kdialog";
}

std::optional<std::filesystem::path> ShowOpenDialog(const std::string& title, const std::vector<FileFilter>& filters,
                                                    const std::filesystem::path& folder) {
    const std::vector<std::string> result = pfd::open_file(title, Utf8(folder), Flatten(filters)).result();
    if (result.empty()) return std::nullopt;
    return FromUtf8(result.front());
}

std::optional<std::filesystem::path> ShowSaveDialog(const std::string& title, const std::filesystem::path& suggested,
                                                    const std::vector<FileFilter>& filters, const std::string& extension) {
    const std::string result = pfd::save_file(title, Utf8(suggested), Flatten(filters)).result();
    if (result.empty()) return std::nullopt;
    std::filesystem::path path = FromUtf8(result);
    if (path.extension() != extension) path += extension;
    return path;
}

} // namespace bz::platform
