#pragma once

#include <filesystem>

namespace bz::platform {

/// Folder with fonts, icons and other read-only assets. Found next to the
/// executable (portable build), in ../share/breezecad (Linux install) or in
/// the app bundle's Resources (macOS).
std::filesystem::path AssetsDir();

/// The running program's file (empty if the system does not say).
std::filesystem::path ExecutablePath();

/// Folder of the running program (breezecad.exe).
std::filesystem::path ExecutableDir();

/// Per-user settings folder, created on demand:
/// Windows %APPDATA%\BreezeCAD, macOS ~/Library/Application Support/BreezeCAD,
/// Linux $XDG_CONFIG_HOME/breezecad.
std::filesystem::path ConfigDir();

/// Where autosave copies live (inside ConfigDir()).
std::filesystem::path AutosaveDir();

/// The user's documents folder, the default place for new files.
std::filesystem::path DocumentsDir();

} // namespace bz::platform
