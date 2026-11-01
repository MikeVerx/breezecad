#pragma once
// Where the installer takes BreezeCAD from.
//
// Online installer: the release's setup-manifest.json from GitHub, then each
// chosen file from the same release, checked against its SHA-256.
// Offline installer: the same manifest and files packed into the installer
// itself — a ZIP appended to the .exe, found by the trailer at its end
// ("BZSETUP1" + the ZIP's offset), so nothing is downloaded at all.
//
// setup-manifest.json (made by scripts/installer/make-manifest.py in CI):
//   { "version": "0.2.5-Beta", "tag": "v0.2.5-beta",
//     "program":   { "file": "BreezeCAD-windows.zip", "size": …, "unpacked": …, "sha256": "…" },
//     "modules":   [ { "id": "mcp", "name": "…", "description": "…", "file": "bz-mcp-…bzmod", "size": …, "sha256": "…" } ],
//     "languages": [ { "id": "de", "file": "lang-de.zip", "size": …, "unpacked": …, "sha256": "…" } ] }

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bz::setup {

struct Component {
    std::string   id;            ///< "program", a module's id, a language code
    std::string   file;
    std::string   name;          ///< modules: as their module.json says (UTF-8)
    std::string   description;
    std::uint64_t size = 0;      ///< to download
    std::uint64_t unpacked = 0;  ///< on disk once unpacked (0: as `size`)
    std::string   sha256;        ///< lower-case hex; empty: not checked
};

struct Manifest {
    std::string            version;
    std::string            tag;
    Component              program;
    std::vector<Component> modules;
    std::vector<Component> languages;

    const Component* Language(const std::string& code) const;
    const Component* Module(const std::string& id) const;
};

/// Whether this installer carries everything inside (the offline one).
bool Offline();
/// This installer's own .exe without the packed files: what is kept as uninstall.exe.
std::span<const std::uint8_t> InstallerExe();

using Progress = std::function<void(std::uint64_t done, std::uint64_t total)>;

/// The manifest: from inside (offline) or from GitHub. `error` in English
/// for the log; the interface says it in its own words.
std::optional<Manifest> LoadManifest(std::string& error);

/// One file of the manifest, checked. `cancel` stops a download.
std::optional<std::vector<std::uint8_t>> Fetch(const Manifest& m, const Component& c, const Progress& progress,
                                               const std::atomic<bool>& cancel, std::string& error);

/// Lower-case hex SHA-256.
std::string Sha256(std::span<const std::uint8_t> bytes);

} // namespace bz::setup
