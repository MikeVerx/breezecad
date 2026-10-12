#include "modules/ModuleManager.h"

#include "core/Log.h"
#include "core/PathText.h"
#include "modules/ModuleHost.h"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <cstddef>
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
    for (const Json& v : root.value("on", Json::array()))
        if (v.is_string()) myOn.push_back(v.get<std::string>());
    for (const Json& v : root.value("crashed", Json::array()))
        if (v.is_object() && v.contains("id") && v["id"].is_string())
            myCrashed.emplace_back(v["id"].get<std::string>(), v.value("problem", ""));
}

void ModuleManager::WriteState() const {
    Json crashed = Json::array();
    for (const auto& [id, problem] : myCrashed) crashed.push_back({{"id", id}, {"problem", problem}});
    const Json      root{{"off", myOff}, {"on", myOn}, {"crashed", std::move(crashed)}};
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
    myHostInfo = host;
    std::error_code                    ec;
    std::vector<std::filesystem::path> folders;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().filename().u8string().starts_with(u8".")) continue;   // ".installing": a package being unpacked
        if (e.is_directory(ec) && std::filesystem::exists(e.path() / "module.json", ec)) folders.push_back(e.path());
    }
    std::sort(folders.begin(), folders.end());
    for (const std::filesystem::path& folder : folders) AddFolder(folder, host);
}

void ModuleManager::AddFolder(const std::filesystem::path& folder, const HostInfo& host) {
    {
        ModuleEntry m;
        m.folder = folder;
        Expected<Manifest> manifest = ReadManifest(folder / "module.json", host.os);
        if (!manifest) {
            const std::u8string name = folder.filename().u8string();
            m.manifest.id = m.manifest.name = std::string(name.begin(), name.end());
            m.state                         = ModuleState::Failed;
            m.problem                       = manifest.GetError().message;
            myModules.push_back(std::move(m));
            return;
        }
        m.manifest = std::move(*manifest);
        if (Find(m.manifest.id)) {
            log::Warn("Модуль «{}» найден дважды, второй пропущен: {}", m.manifest.id, folder.string());
            return;
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
        } else if (!m.manifest.enabledByDefault &&
                   std::find(myOn.begin(), myOn.end(), m.manifest.id) == myOn.end()) {
            m.state = ModuleState::Off;   // waits for the user's consent
        }
        myModules.push_back(std::move(m));
    }
}

bool ModuleManager::Removable(const ModuleEntry& m) const {
    if (myInstallDir.empty()) return false;
    std::error_code ec;
    return std::filesystem::equivalent(m.folder.parent_path(), myInstallDir, ec);
}

namespace {

/// A path inside a package that stays inside the folder it is unpacked to.
bool SafeEntry(std::string_view name) {
    if (name.empty() || name.front() == '/' || name.find(':') != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t end = std::min(name.find('/', start), name.size());
        if (name.substr(start, end - start) == "..") return false;
        start = end + 1;
    }
    return true;
}

std::filesystem::path FromUtf8(std::string_view s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::string OsTitle(std::string_view os) {
    if (os == "windows") return "Windows";
    if (os == "linux") return "Linux";
    if (os == "macos") return "macOS";
    return std::string(os);
}

} // namespace

Expected<std::string> ModuleManager::Install(const std::filesystem::path& package) {
    if (myInstallDir.empty()) return Error{"Папка модулей не задана"};
    std::string bytes;
    {
        std::ifstream in(package, std::ios::binary);   // a wide path on Windows: the name may be Cyrillic
        if (!in) return Error{"Не удалось прочитать файл модуля"};
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
        return Error{"Это не модуль BreezeCAD: файл не ZIP-архив (нужен .bzmod)"};
    struct Close {
        mz_zip_archive& z;
        ~Close() { mz_zip_reader_end(&z); }
    } close{zip};

    // The files, and where module.json is: at the top or in the only folder.
    struct Entry {
        unsigned    index;
        std::string name;
    };
    std::vector<Entry>       files;
    std::vector<std::string> manifests;   // the folder prefixes that have one
    const unsigned count = mz_zip_reader_get_num_files(&zip);
    for (unsigned i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) return Error{"Архив модуля повреждён"};
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        std::string name = st.m_filename;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (!SafeEntry(name)) return Error{std::format("Архив модуля небезопасен: путь «{}» ведёт за пределы папки модуля", name)};
        if (name == "module.json") manifests.emplace_back();
        else if (name.ends_with("/module.json") && std::count(name.begin(), name.end(), '/') == 1)
            manifests.push_back(name.substr(0, name.size() - std::string_view("module.json").size()));
        files.push_back({i, std::move(name)});
    }
    std::string prefix;
    if (std::ranges::find(manifests, std::string()) != manifests.end()) prefix.clear();
    else if (manifests.size() == 1) prefix = manifests.front();
    else if (manifests.empty()) return Error{"В архиве нет module.json — это не модуль BreezeCAD"};
    else return Error{"В архиве несколько модулей — устанавливайте их по одному"};

    // Unpack next to the installed modules (the same disk: the final move is a rename).
    std::error_code ec;
    std::filesystem::create_directories(myInstallDir, ec);
    const std::filesystem::path staging = myInstallDir / ".installing";
    std::filesystem::remove_all(staging, ec);
    const auto fail = [&](std::string message) -> Expected<std::string> {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        return Error{std::move(message)};
    };
    if (!std::filesystem::create_directories(staging, ec)) return fail(std::format("Не удалось создать папку в {}", ToUtf8(myInstallDir)));
    for (const Entry& f : files) {
        if (!f.name.starts_with(prefix) || f.name.size() == prefix.size()) continue;
        const std::filesystem::path target = staging / FromUtf8(std::string_view(f.name).substr(prefix.size()));
        std::filesystem::create_directories(target.parent_path(), ec);
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, f.index, &size, 0);
        if (!data) return fail(std::format("Архив модуля повреждён: не читается «{}»", f.name));
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        mz_free(data);
        if (!out) return fail("Не удалось записать файлы модуля (диск заполнен?)");
    }

    // Check before anything installed is touched.
    Expected<Manifest> manifest = ReadManifest(staging / "module.json", myHostInfo.os);
    if (!manifest) return fail(manifest.GetError().message);
    std::vector<std::string> missing = MissingRequirements(manifest->requirements, myHostInfo);
    if (manifest->abiVersion != BZ_MODULE_ABI_VERSION)
        missing.insert(missing.begin(), std::format("модуль собран для интерфейса модулей версии {}, а эта программа — {}",
                                                    manifest->abiVersion, BZ_MODULE_ABI_VERSION));
    if (!missing.empty()) return fail(std::format("Модуль «{}» не подходит: {}", manifest->name, Join(missing)));
    if (!std::filesystem::is_regular_file(staging / FromUtf8(manifest->library), ec))
        return fail(std::format("В архиве нет библиотеки {} — это архив модуля для другой системы, нужен для {}", manifest->library,
                                OsTitle(myHostInfo.os)));

    const std::string id = manifest->id;
    if (ModuleEntry* old = FindMutable(id)) {
        if (!Removable(*old)) return fail(std::format("Модуль «{}» встроен в программу и обновляется вместе с ней", old->manifest.name));
        Unload(*old);
        std::filesystem::remove_all(old->folder, ec);
        if (ec) return fail(std::format("Не удалось заменить прежнюю версию модуля «{}»: {}", old->manifest.name, ec.message()));
        std::erase_if(myModules, [&](const ModuleEntry& m) { return &m == old; });
    }
    const std::filesystem::path folder = myInstallDir / FromUtf8(id);
    std::filesystem::remove_all(folder, ec);   // a folder left without module.json
    std::filesystem::rename(staging, folder, ec);
    if (ec) return fail(std::format("Не удалось поставить модуль на место: {}", ec.message()));
    Erase(myCrashed, id);   // a new version: the old crash note is about the old one
    AddFolder(folder, myHostInfo);
    TurnOn(id);             // its state (and why it did not start, if so) is in the list
    log::Info("Модуль «{}» {} установлен", manifest->name, manifest->version);
    return id;
}

Status ModuleManager::Uninstall(const std::string& id) {
    ModuleEntry* m = FindMutable(id);
    if (!m) return Error{"Нет такого модуля"};
    if (!Removable(*m)) return Error{"Встроенный модуль удалить нельзя — его можно выключить"};
    Unload(*m);
    std::error_code ec;
    std::filesystem::remove_all(m->folder, ec);
    if (ec) return Error{std::format("Не удалось удалить папку модуля: {}", ec.message())};
    std::erase_if(myModules, [&](const ModuleEntry& e) { return &e == m; });
    Erase(myOff, id);
    Erase(myOn, id);
    Erase(myCrashed, id);
    WriteState();
    return {};
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
    const bool         fits = api && api->abi_version == BZ_MODULE_ABI_VERSION && api->struct_size >= offsetof(BzModuleApi, main_tick) &&   // 0.2 modules end there
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
    Erase(myOn, id);
    WriteState();
    return true;
}

bool ModuleManager::TurnOn(const std::string& id) {
    ModuleEntry* m = FindMutable(id);
    if (!m) return false;
    Erase(myOff, id);
    Erase(myCrashed, id);
    if (!m->manifest.enabledByDefault && std::find(myOn.begin(), myOn.end(), id) == myOn.end()) myOn.push_back(id);
    WriteState();
    if (m->state == ModuleState::Off || m->state == ModuleState::Crashed || m->state == ModuleState::Failed) {
        m->state = ModuleState::Found;
        m->problem.clear();
    }
    Load(*m);
    return m->state == ModuleState::Loaded;
}

bool ModuleManager::Tick() {
    if (!TakeMainRequest()) return false;
    bool ran = false;
    for (ModuleEntry& m : myModules) {
        if (m.state != ModuleState::Loaded || !BZ_HAS(m.api, main_tick)) continue;
        ran = Call(m.manifest.id, [](const BzModuleApi& api) { api.main_tick(); }) || ran;
    }
    return ran;
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
