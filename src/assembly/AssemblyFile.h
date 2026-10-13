#pragma once
// An assembly in a .bzc file: the same container as a part (manifest +
// document.json), with documentType "assembly". See docs/ASSEMBLY_FORMAT.md.

#include "assembly/Assembly.h"
#include "core/Expected.h"

#include <filesystem>
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

} // namespace bz::assembly
