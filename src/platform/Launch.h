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

} // namespace bz::platform
