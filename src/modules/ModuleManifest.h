#pragma once
// module.json: what a module is, which file to load and what it needs from
// the computer. Read before anything of the module is loaded, so a module
// that cannot run here is never loaded at all.
//
//   {
//     "id": "hello",                       // latin letters, digits, '-', '_', '.'
//     "name": "Пример модуля",
//     "version": "1.0.0",
//     "description": "…",
//     "abiVersion": 1,                     // BZ_MODULE_ABI_VERSION it was built with
//     "library": "hello",                  // -> hello.dll / libhello.so / libhello.dylib
//     "enabledByDefault": true,            // false: stays off until the user turns it on
//     // or per system: {"windows": "hello.dll", "linux": "libhello.so", "macos": "…"}
//     "requirements": {
//       "os": ["windows", "linux", "macos"],  // empty or missing: any
//       "minHostVersion": "0.2.0",
//       "minMemoryMb": 4096,               // installed RAM
//       "minCpuCores": 4,
//       "minOpenGL": "3.3"                 // checked only when the GL version is known
//     }
//   }

#include "core/Expected.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bz::modules {

struct Requirements {
    std::vector<std::string> os;            ///< "windows", "linux", "macos"; empty: any
    std::string              minHostVersion;
    unsigned                 minMemoryMb = 0;
    unsigned                 minCpuCores = 0;
    std::string              minOpenGL;     ///< "3.3"; empty: none
};

struct Manifest {
    std::string  id;
    std::string  name;
    std::string  version;
    std::string  description;
    unsigned     abiVersion = 0;
    std::string  library;                   ///< file name for this system
    bool         enabledByDefault = true;   ///< false: off until turned on (e.g. it opens a port)
    Requirements requirements;
};

/// The computer the program runs on. Unknown values are 0 / empty and
/// never make a requirement fail.
struct HostInfo {
    std::string os;                         ///< "windows", "linux", "macos"
    std::string hostVersion;                ///< "0.2.0"
    unsigned    memoryMb = 0;
    unsigned    cpuCores = 0;
    std::string openGL;                     ///< "4.6"; empty until the viewport is up

    /// This computer (OpenGL left empty: the viewport fills it in).
    static HostInfo Detect();
};

/// Parses module.json; `os` picks the library name ("linux" -> libX.so).
Expected<Manifest> ParseManifest(std::string_view json, std::string_view os);
Expected<Manifest> ReadManifest(const std::filesystem::path& file, std::string_view os);

/// Everything the computer lacks, in Russian, one line each; empty: it can run.
std::vector<std::string> MissingRequirements(const Requirements& r, const HostInfo& host);

/// "1.10.2" vs "1.9": -1, 0, 1; missing parts count as 0, text after digits is ignored.
int CompareVersions(std::string_view a, std::string_view b);

} // namespace bz::modules
