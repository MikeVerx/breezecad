#pragma once
// Hands a web address or a folder to the system (browser, file manager).
// The program itself never goes online: this only opens what the user clicked.

#include <filesystem>
#include <string>

namespace bz::platform {

/// Opens `url` (https://...) in the default browser. False if it could not start.
bool OpenUrl(const std::string& url);
/// Shows `folder` in the file manager, creating it first if needed.
bool OpenFolder(const std::filesystem::path& folder);
/// The file manager at `file`'s folder with the file selected (Windows,
/// macOS); on Linux the folder alone (file managers differ). Its folder
/// when the file is gone.
bool ShowInFolder(const std::filesystem::path& file);

} // namespace bz::platform
