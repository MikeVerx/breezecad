#pragma once
// Finds modules, checks what they need, loads, switches off and unloads them.
//
// Layout: <modules dir>/<any folder>/module.json + the library it names.
// The user's choices (switched off) and the modules that crashed are kept
// in one small JSON file (`statePath`, in the config folder).
//
// A crash inside a module cannot be caught in the same process, so every
// call into a module is bracketed by a marker file next to `statePath`
// ("modules-running": the module's id). The marker is removed when the
// call returns. If the program dies inside the call, the next start finds
// the marker, switches that module off and remembers why; the user turns
// it on again by hand when a fixed version is installed.

#include "modules/DynamicLibrary.h"
#include "modules/ModuleManifest.h"
#include "modules/bz_module.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bz::modules {

enum class ModuleState : unsigned char {
    Found,         ///< read, not loaded yet
    Loaded,        ///< running
    Off,           ///< switched off by the user
    Unsupported,   ///< this computer lacks something (see `problem`)
    Failed,        ///< did not load or init() refused (see `problem`)
    Crashed,       ///< crashed earlier and was switched off (see `problem`)
};

struct ModuleEntry {
    std::filesystem::path folder;
    Manifest              manifest;
    ModuleState           state = ModuleState::Found;
    std::string           problem;   ///< Russian, for the user

    DynamicLibrary     library;
    const BzModuleApi* api = nullptr;
};

class ModuleManager {
public:
    /// `statePath`: where choices and crash notes are kept (its folder must exist or be creatable).
    ModuleManager(std::filesystem::path statePath, const BzHostApi* host);
    ~ModuleManager();
    ModuleManager(const ModuleManager&)            = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /// Reads every module.json under `dir` (one level of folders). Notes a
    /// module that crashed during the previous run. Loads nothing.
    void Discover(const std::filesystem::path& dir, const HostInfo& host);

    /// Loads every module that is not off, unsupported or crashed.
    void LoadAll();
    /// Switches a module off (shutdown, unload) and remembers it.
    bool TurnOff(const std::string& id);
    /// Switches a module on again (also clears a crash note) and loads it.
    bool TurnOn(const std::string& id);
    /// Unloads everything (shutdown first); the choices stay.
    void UnloadAll();

    /// Runs `call` into module `id` under the crash marker; false when the
    /// module is not loaded. Use for every call into a module's functions.
    bool Call(const std::string& id, const std::function<void(const BzModuleApi&)>& call);

    const std::vector<ModuleEntry>& Modules() const { return myModules; }
    const ModuleEntry*              Find(const std::string& id) const;

    /// Id of the module the previous run died in; empty when it ended normally.
    const std::string& CrashedLastRun() const { return myCrashedLastRun; }

    /// The marker file (for tests and diagnostics).
    std::filesystem::path MarkerPath() const;

private:
    ModuleEntry* FindMutable(const std::string& id);
    void         Load(ModuleEntry& m);
    void         Unload(ModuleEntry& m);
    void         ReadState();
    void         WriteState() const;
    void         SetMarker(const std::string& id) const;
    void         ClearMarker() const;

    std::filesystem::path    myStatePath;
    const BzHostApi*         myHost;
    std::vector<ModuleEntry> myModules;
    std::vector<std::string> myOff;                                  ///< switched off by the user
    std::vector<std::pair<std::string, std::string>> myCrashed;      ///< id, what happened
    std::string              myCrashedLastRun;
};

} // namespace bz::modules
