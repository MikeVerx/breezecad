#pragma once
// An assembly in a .bzc file: the same container as a part (manifest +
// document.json), with documentType "assembly". See docs/ASSEMBLY_FORMAT.md.

#include "assembly/Assembly.h"
#include "core/Expected.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bz::assembly {

inline constexpr int kAssemblyFormatVersion = 1;

std::string        AssemblyToJson(const Assembly& assembly);
/// `warnings`: non-fatal problems (unknown constraint types are skipped).
Expected<Assembly> AssemblyFromJson(std::string_view json, std::vector<std::string>* warnings = nullptr);

Status             SaveAssembly(const Assembly& assembly, const std::filesystem::path& path, std::string_view generator);
Expected<Assembly> LoadAssembly(const std::filesystem::path& path, std::vector<std::string>* warnings = nullptr);

/// How a component refers to `part` from an assembly saved at `assemblyFile`:
/// relative with '/' when possible, absolute when they are on different
/// drives or the assembly is not saved yet (empty `assemblyFile`).
std::string RelativePartPath(const std::filesystem::path& assemblyFile, const std::filesystem::path& part);
/// The part file a component refers to.
std::filesystem::path ResolvePartPath(const std::filesystem::path& assemblyFile, std::string_view componentPath);
/// Paths of components made while the assembly had no file are absolute:
/// makes them relative to where it is saved now.
void RebasePaths(Assembly& assembly, const std::filesystem::path& oldFile, const std::filesystem::path& newFile);

// --- The parts go with the assembly ------------------------------------------
//
// An assembly carried to another computer or phone (its folder copied, the
// projects folder synced) must find its parts. So every part lies in the
// assembly's folder: a part from elsewhere is copied there as
// "<деталь> - <сборка>.bzc" and the component refers to the copy.

/// Where the assembly's folder takes new files (the platform decides: on
/// Android the projects folder is the only place a file reaches).
struct PartFolder {
    /// A new file for `name` in the assembly's folder: a free name like it
    /// ("Вал - Сборка (2).bzc" when "Вал - Сборка.bzc" is taken). Empty: the
    /// folder takes no new files.
    std::function<std::filesystem::path(const std::string& name)> newFile;
    /// After a copy was written (Android: copies it to its document). False: it did not get there.
    std::function<bool(const std::filesystem::path&)> written;
    /// Whether a part in a folder under the assembly's counts as with it
    /// (the desktop: "Стандартные изделия/"); not on Android (a flat folder).
    bool subfolders = true;
};

/// "Вал - Узел.bzc": the copy of `part` for the assembly `assemblyFile`. A
/// copy made for another assembly ("Вал - Узел.bzc" for "Узел 2") is not
/// named twice: "Вал - Узел 2.bzc".
std::string CopyName(const std::filesystem::path& part, const std::filesystem::path& assemblyFile,
                     const std::filesystem::path& previousAssemblyFile = {});
/// Whether `part` lies in the folder of `assemblyFile` (or under it, `subfolders`).
bool LiesWith(const std::filesystem::path& assemblyFile, const std::filesystem::path& part, bool subfolders);

struct GatherResult {
    std::vector<std::string> copied;     ///< names of the copies made
    std::vector<std::string> problems;   ///< for the user; the parts concerned stay where they were
    bool ok = true;                      ///< false: a copy could not be made — the assembly must not be saved
};
/// Before the assembly is saved at `to` (was at `from`, empty when new):
/// every component's part that does not lie with it is copied there, the
/// components refer to the copies; all paths become relative to `to`. Equal
/// components share one copy; a copy already there with the same content is
/// used again. A part that is not found stays as it was (a problem, not a failure).
GatherResult GatherParts(Assembly& assembly, const std::filesystem::path& from, const std::filesystem::path& to,
                         const PartFolder& folder);

} // namespace bz::assembly
