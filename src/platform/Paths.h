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

/// Whether this copy was put here by the installer (Windows: installed.json
/// next to the program). An installed copy keeps modules and languages in
/// ConfigDir(); a portable one (unpacked from the archive) next to itself.
bool InstalledCopy();

/// The user's modules: "module" next to a portable program (Windows, Linux),
/// else ConfigDir()/modules. Packages (.bzmod) dropped there are installed
/// on start. Created on demand.
std::filesystem::path ModulesDir();

/// The user's language packs: "language" next to a portable program, else
/// ConfigDir()/lang. Created on demand.
std::filesystem::path LanguagesDir();

/// Where autosave copies live (inside ConfigDir()).
std::filesystem::path AutosaveDir();

/// The user's documents folder, the default place for new files.
std::filesystem::path DocumentsDir();

} // namespace bz::platform
