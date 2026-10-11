#include "modules/ModuleManager.h"

#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>
#include <type_traits>

namespace bz::modules {

using Json = nlohmann::json;

namespace {

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string Join(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += (out.empty() ? "" : "; ") + l;
    return out;
}

template <class C>
void Erase(C& c, const std::string& id) {
    std::erase_if(c, [&](const auto& v) {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, std::string>) return v == id;
        else return v.first == id;
    });
}

} // namespace

ModuleManager::ModuleManager(std::filesystem::path statePath, const BzHostApi* host)
    : myStatePath(std::move(statePath)), myHost(host) {
    ReadState();
    // The previous run died inside a module: switch that module off.
    std::string id = ReadText(MarkerPath());
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r')) id.pop_back();
    if (!id.empty()) {
        Erase(myCrashed, id);
        myCrashed.emplace_back(id, "BreezeCAD аварийно закрылся во время работы модуля — модуль отключён");
        log::Warn("Модуль «{}» упал в прошлый раз и отключён", id);
        myCrashedLastRun = id;
        WriteState();
    }
    ClearMarker();
}

ModuleManager::~ModuleManager() { UnloadAll(); }

std::filesystem::path ModuleManager::MarkerPath() const { return myStatePath.parent_path() / "modules-running"; }

void ModuleManager::SetMarker(const std::string& id) const {
    std::error_code ec;
    std::filesystem::create_directories(myStatePath.parent_path(), ec);
    std::ofstream out(MarkerPath(), std::ios::binary | std::ios::trunc);
    out << id;
    out.flush();   // must be on disk before the module runs
}

void ModuleManager::ClearMarker() const {
    std::error_code ec;
    std::filesystem::remove(MarkerPath(), ec);
}

void ModuleManager::ReadState() {
    const Json root = Json::parse(ReadText(myStatePath), nullptr, false);
    if (root.is_discarded() || !root.is_object()) return;
    for (const Json& v : root.value("off", Json::array()))
        if (v.is_string()) myOff.push_back(v.get<std::string>());
    for (const Json& v : root.value("crashed", Json::array()))
        if (v.is_object() && v.contains("id") && v["id"].is_string())
            myCrashed.emplace_back(v["id"].get<std::string>(), v.value("problem", ""));
}

void ModuleManager::WriteState() const {
    Json crashed = Json::array();
    for (const auto& [id, problem] : myCrashed) crashed.push_back({{"id", id}, {"problem", problem}});
    const Json      root{{"off", myOff}, {"crashed", std::move(crashed)}};
    std::error_code ec;
    std::filesystem::create_directories(myStatePath.parent_path(), ec);
    std::ofstream out(myStatePath, std::ios::binary | std::ios::trunc);
    out << root.dump(1, '\t');
    if (!out) log::Warn("Не удалось сохранить состояние модулей: {}", myStatePath.string());
}

ModuleEntry* ModuleManager::FindMutable(const std::string& id) {
    for (ModuleEntry& m : myModules)
        if (m.manifest.id == id) return &m;
    return nullptr;
}

const ModuleEntry* ModuleManager::Find(const std::string& id) const {
    for (const ModuleEntry& m : myModules)
        if (m.manifest.id == id) return &m;
    return nullptr;
}

void ModuleManager::Discover(const std::filesystem::path& dir, const HostInfo& host) {
    std::error_code                    ec;
    std::vector<std::filesystem::path> folders;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_directory(ec) && std::filesystem::exists(e.path() / "module.json", ec)) folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());

    for (const std::filesystem::path& folder : folders) {
        ModuleEntry m;
        m.folder = folder;
        Expected<Manifest> manifest = ReadManifest(folder / "module.json", host.os);
        if (!manifest) {
            const std::u8string name = folder.filename().u8string();
            m.manifest.id = m.manifest.name = std::string(name.begin(), name.end());
            m.state                         = ModuleState::Failed;
            m.problem                       = manifest.GetError().message;
            myModules.push_back(std::move(m));
            continue;
        }
        m.manifest = std::move(*manifest);
        if (Find(m.manifest.id)) {
            log::Warn("Модуль «{}» найден дважды, второй пропущен: {}", m.manifest.id, folder.string());
            continue;
        }
        const auto crashed = std::find_if(myCrashed.begin(), myCrashed.end(),
                                          [&](const auto& c) { return c.first == m.manifest.id; });
        std::vector<std::string> missing = MissingRequirements(m.manifest.requirements, host);
        if (m.manifest.abiVersion != BZ_MODULE_ABI_VERSION)
            missing.insert(missing.begin(), std::format("Модуль собран для интерфейса модулей версии {}, а нужна {}",
                                                        m.manifest.abiVersion, BZ_MODULE_ABI_VERSION));
        if (crashed != myCrashed.end()) {
            m.state   = ModuleState::Crashed;
            m.problem = crashed->second;
        } else if (!missing.empty()) {
            m.state   = ModuleState::Unsupported;
            m.problem = Join(missing);
        } else if (std::find(myOff.begin(), myOff.end(), m.manifest.id) != myOff.end()) {
            m.state = ModuleState::Off;
        }
        myModules.push_back(std::move(m));
    }
}

void ModuleManager::Load(ModuleEntry& m) {
    if (m.state != ModuleState::Found) return;
    Expected<DynamicLibrary> lib = DynamicLibrary::Open(m.folder / m.manifest.library);
    if (!lib) {
        m.state   = ModuleState::Failed;
        m.problem = lib.GetError().message;
        log::Warn("Модуль «{}»: {}", m.manifest.id, m.problem);
        return;
    }
    void* symbol = lib->Symbol(BZ_MODULE_ENTRY_NAME);
    if (!symbol) {
        m.state   = ModuleState::Failed;
        m.problem = "В библиотеке нет функции bz_module_entry — это не модуль BreezeCAD";
        return;
    }
    BzModuleEntryFn entry = nullptr;
    static_assert(sizeof(entry) == sizeof(symbol));
    std::memcpy(&entry, &symbol, sizeof(entry));

    SetMarker(m.manifest.id);
    const BzModuleApi* api = entry();
    int32_t            rc  = BZ_EFAIL;
    const bool         fits = api && api->abi_version == BZ_MODULE_ABI_VERSION && api->struct_size >= sizeof(BzModuleApi) &&
                      api->init && api->shutdown;
    if (fits) rc = api->init(myHost);
    ClearMarker();

    if (!fits) {
        m.state   = ModuleState::Failed;
        m.problem = std::format("Модуль собран для другого интерфейса модулей (нужна версия {})", BZ_MODULE_ABI_VERSION);
        return;
    }
    if (rc != BZ_OK) {
        m.state   = ModuleState::Failed;
        m.problem = std::format("Модуль не запустился (код {})", rc);
        return;
    }
    m.library = std::move(*lib);
    m.api     = api;
    m.state   = ModuleState::Loaded;
    m.problem.clear();
    log::Info("Модуль «{}» {} загружен", m.manifest.name, m.manifest.version);
}

void ModuleManager::Unload(ModuleEntry& m) {
    if (m.state != ModuleState::Loaded) return;
    SetMarker(m.manifest.id);
    m.api->shutdown();
    ClearMarker();
    m.api = nullptr;
    m.library.Close();
    m.state = ModuleState::Found;
}

void ModuleManager::LoadAll() {
    for (ModuleEntry& m : myModules) Load(m);
}

void ModuleManager::UnloadAll() {
    for (auto it = myModules.rbegin(); it != myModules.rend(); ++it) Unload(*it);   // last loaded first
}

bool ModuleManager::TurnOff(const std::string& id) {
    ModuleEntry* m = FindMutable(id);
    if (!m) return false;
    Unload(*m);
    if (m->state == ModuleState::Found || m->state == ModuleState::Failed) m->state = ModuleState::Off;
    if (std::find(myOff.begin(), myOff.end(), id) == myOff.end()) myOff.push_back(id);
    WriteState();
    return true;
}

bool ModuleManager::TurnOn(const std::string& id) {
    ModuleEntry* m = FindMutable(id);
    if (!m) return false;
    Erase(myOff, id);
    Erase(myCrashed, id);
    WriteState();
    if (m->state == ModuleState::Off || m->state == ModuleState::Crashed || m->state == ModuleState::Failed) {
        m->state = ModuleState::Found;
        m->problem.clear();
    }
    Load(*m);
    return m->state == ModuleState::Loaded;
}

bool ModuleManager::Call(const std::string& id, const std::function<void(const BzModuleApi&)>& call) {
    ModuleEntry* m = FindMutable(id);
    if (!m || m->state != ModuleState::Loaded) return false;
    SetMarker(id);
    call(*m->api);
    ClearMarker();
    return true;
}

} // namespace bz::modules
