#include "assembly/Assembly.h"
#include "kernel/AssemblyModel.h"
#include "kernel/AssemblyReadApi.h"
#include "modules/ModuleHost.h"
#include "modules/ModuleManager.h"
#include "modules/ModuleManifest.h"

#include <doctest.h>

#include <cstring>
#include <format>
#include <filesystem>
#include <fstream>
#include <string>

using namespace bz;
using namespace bz::modules;
namespace fs = std::filesystem;

namespace {

const char* kGood = R"({"id": "hello", "name": "Пример", "version": "1.0.0", "abiVersion": 1, "library": "hello",
    "requirements": {"os": ["linux", "windows"], "minHostVersion": "0.2.0", "minMemoryMb": 1024,
                     "minCpuCores": 2, "minOpenGL": "3.3"}})";

HostInfo Roomy() { return HostInfo{"linux", "0.3.0", 16000, 8, "4.6"}; }

void WriteFile(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

std::string ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

/// <tmp>/bz_modules/{mods, config/modules.json}; a module folder per call to Add.
struct Fixture {
    fs::path root  = fs::temp_directory_path() / "bz_modules";
    fs::path mods  = root / "mods";
    fs::path state = root / "config" / "modules.json";

    Fixture() {
        fs::remove_all(root);
        fs::create_directories(mods);
    }
    ~Fixture() { fs::remove_all(root); }

    /// Folder `id` with a copy of `library` and a manifest naming it.
    void Add(const std::string& id, const fs::path& library, unsigned abi = 1, unsigned memoryMb = 0) {
        const fs::path dir = mods / id;
        fs::create_directories(dir);
        fs::copy_file(library, dir / library.filename(), fs::copy_options::overwrite_existing);
        const std::u8string file = library.filename().u8string();
        WriteFile(dir / "module.json",
                  std::format(R"({{"id": "{}", "version": "1.0", "abiVersion": {}, "library": "{}",
                                  "requirements": {{"minMemoryMb": {}}}}})",
                              id, abi, std::string(file.begin(), file.end()), memoryMb));
    }
};

} // namespace

TEST_CASE("module manifest: fields, library name per system, errors") {
    const auto m = ParseManifest(kGood, "linux");
    REQUIRE(m);
    CHECK(m->id == "hello");
    CHECK(m->name == "Пример");
    CHECK(m->abiVersion == 1);
    CHECK(m->library == "libhello.so");
    CHECK(m->requirements.minMemoryMb == 1024);
    CHECK(m->requirements.os.size() == 2);
    CHECK(ParseManifest(kGood, "windows")->library == "hello.dll");
    CHECK(ParseManifest(kGood, "macos")->library == "libhello.dylib");

    const auto perOs = ParseManifest(R"({"id": "x", "abiVersion": 1, "library": {"linux": "libx-gtk.so"}})", "linux");
    REQUIRE(perOs);
    CHECK(perOs->name == "x");   // no name: the id
    CHECK(perOs->library == "libx-gtk.so");
    CHECK_FALSE(ParseManifest(R"({"id": "x", "abiVersion": 1, "library": {"linux": "libx.so"}})", "windows"));

    CHECK_FALSE(ParseManifest("not json", "linux"));
    CHECK_FALSE(ParseManifest(R"({"abiVersion": 1, "library": "x"})", "linux"));                  // no id
    CHECK_FALSE(ParseManifest(R"({"id": "../x", "abiVersion": 1, "library": "x"})", "linux"));    // path in id
    CHECK_FALSE(ParseManifest(R"({"id": "x", "library": "x"})", "linux"));                        // no ABI
    CHECK_FALSE(ParseManifest(R"({"id": "x", "abiVersion": 1, "library": "../x.so"})", "linux")); // outside its folder
    CHECK_FALSE(ParseManifest(R"({"id": "x", "abiVersion": 1, "library": "x", "requirements": {"minMemoryMb": "a"}})", "linux"));
}

TEST_CASE("module requirements against the computer") {
    const Requirements r = ParseManifest(kGood, "linux")->requirements;
    CHECK(MissingRequirements(r, Roomy()).empty());
    CHECK(MissingRequirements(r, HostInfo{}).empty());   // nothing known: nothing blocks

    HostInfo small{"macos", "0.1.5", 512, 1, "2.1"};
    const auto missing = MissingRequirements(r, small);
    CHECK(missing.size() == 5);   // system, version, memory, cores, OpenGL
    CHECK(missing[0].find("macos") != std::string::npos);

    CHECK(CompareVersions("0.10.0", "0.9.9") == 1);
    CHECK(CompareVersions("1.0", "1.0.0") == 0);
    CHECK(CompareVersions("3.3", "4.6") == -1);
    CHECK(CompareVersions("0.2.0-beta", "0.2") == 0);

    const HostInfo here = HostInfo::Detect();
    CHECK_FALSE(here.os.empty());
    CHECK(here.memoryMb > 0);
    CHECK(here.cpuCores > 0);
}

TEST_CASE("modules load, switch off and on, and remember it") {
    Fixture f;
    f.Add("hello", BZ_EXAMPLE_MODULE);
    {
        ModuleManager mm(f.state, HostApi());
        mm.Discover(f.mods, Roomy());
        REQUIRE(mm.Modules().size() == 1);
        CHECK(mm.Find("hello")->state == ModuleState::Found);
        mm.LoadAll();
        CHECK(mm.Find("hello")->state == ModuleState::Loaded);

        bool called = false;
        CHECK(mm.Call("hello", [&](const BzModuleApi& api) {
            called = api.abi_version == BZ_MODULE_ABI_VERSION && fs::exists(mm.MarkerPath());
        }));
        CHECK(called);
        CHECK_FALSE(fs::exists(mm.MarkerPath()));
        CHECK_FALSE(mm.Call("nothing", [](const BzModuleApi&) {}));

        CHECK(mm.TurnOff("hello"));
        CHECK(mm.Find("hello")->state == ModuleState::Off);
        CHECK(ReadFile(f.state).find("hello") != std::string::npos);
    }
    {
        ModuleManager mm(f.state, HostApi());   // a new run: still off
        mm.Discover(f.mods, Roomy());
        mm.LoadAll();
        CHECK(mm.Find("hello")->state == ModuleState::Off);
        CHECK(mm.TurnOn("hello"));
        CHECK(mm.Find("hello")->state == ModuleState::Loaded);
        mm.UnloadAll();
        CHECK(mm.Find("hello")->state == ModuleState::Found);
    }
}

TEST_CASE("a module that crashed last time is switched off") {
    Fixture f;
    f.Add("hello", BZ_EXAMPLE_MODULE);
    fs::create_directories(f.state.parent_path());
    WriteFile(f.state.parent_path() / "modules-running", "hello\n");   // as if the program died inside it
    {
        ModuleManager mm(f.state, HostApi());
        CHECK_FALSE(fs::exists(mm.MarkerPath()));
        CHECK(mm.CrashedLastRun() == "hello");
        mm.Discover(f.mods, Roomy());
        mm.LoadAll();
        const ModuleEntry* m = mm.Find("hello");
        CHECK(m->state == ModuleState::Crashed);
        CHECK_FALSE(m->problem.empty());
    }
    {
        ModuleManager mm(f.state, HostApi());   // remembered across runs
        mm.Discover(f.mods, Roomy());
        CHECK(mm.Find("hello")->state == ModuleState::Crashed);
        CHECK(mm.TurnOn("hello"));               // the user turns it on again
        CHECK(mm.Find("hello")->state == ModuleState::Loaded);
    }
    ModuleManager mm(f.state, HostApi());
    mm.Discover(f.mods, Roomy());
    CHECK(mm.Find("hello")->state == ModuleState::Found);   // the note is gone
}

TEST_CASE("modules that cannot run are not loaded") {
    Fixture f;
    f.Add("refuses", BZ_EXAMPLE_MODULE_FAIL);
    f.Add("future", BZ_EXAMPLE_MODULE, 2);
    f.Add("hungry", BZ_EXAMPLE_MODULE, 1, 64000);
    fs::create_directories(f.mods / "broken");
    WriteFile(f.mods / "broken" / "module.json", "{");
    fs::create_directories(f.mods / "missing");
    WriteFile(f.mods / "missing" / "module.json", R"({"id": "missing", "abiVersion": 1, "library": "nothing.so"})");

    ModuleManager mm(f.state, HostApi());
    mm.Discover(f.mods, Roomy());
    mm.LoadAll();
    REQUIRE(mm.Modules().size() == 5);
    CHECK(mm.Find("refuses")->state == ModuleState::Failed);
    CHECK(mm.Find("refuses")->problem.find("код") != std::string::npos);
    CHECK(mm.Find("future")->state == ModuleState::Unsupported);
    CHECK(mm.Find("hungry")->state == ModuleState::Unsupported);
    CHECK(mm.Find("hungry")->problem.find("64000") != std::string::npos);
    CHECK(mm.Find("broken")->state == ModuleState::Failed);
    CHECK(mm.Find("missing")->state == ModuleState::Failed);
    CHECK_FALSE(mm.TurnOn("hungry"));   // the computer still lacks memory
}

TEST_CASE("C read API answers about the active assembly") {
    const BzHostApi* host = HostApi();
    REQUIRE(host->struct_size == sizeof(BzHostApi));
    CHECK(std::strlen(host->host_version()) > 0);

    SetActiveAssembly(nullptr);
    int32_t count = -1;
    CHECK(host->component_count(&count) == BZ_ENODATA);

    assembly::Assembly a;
    a.info = {"Узел", "БРЗ.100"};
    const ObjectId id = a.AddComponent("parts/nothing.bzc", "Нет", {assembly::Rotation{}, {10, 20, 30}}).id;
    a.FindComponent(id)->properties.material = "Латунь";
    kernel::AssemblyModel geometry;
    geometry.Update(a, fs::temp_directory_path() / "bz_modules_api.bza");
    const kernel::AssemblyReader reader(a, geometry);
    SetActiveAssembly(&reader);

    const char *name = nullptr, *designation = nullptr;
    CHECK(host->assembly_name(&name, &designation) == BZ_OK);
    CHECK(std::string(designation) == "БРЗ.100");
    CHECK(host->component_count(&count) == BZ_OK);
    CHECK(count == 1);

    BzComponentInfo c{};
    REQUIRE(host->component_at(0, &c) == BZ_OK);
    CHECK(c.id == id.value);
    CHECK(std::string(c.tree_name) == "Нет:1");
    CHECK(std::string(c.material) == "Латунь");
    CHECK(c.origin[2] == doctest::Approx(30));
    CHECK(c.z_axis[2] == doctest::Approx(1));
    CHECK(c.has_geometry == 0);
    CHECK(host->component_at(1, &c) == BZ_ENOTFOUND);
    CHECK(host->component_at(0, nullptr) == BZ_EARGUMENT);

    double lo[3], hi[3];
    CHECK(host->component_bounds(id.value, lo, hi) == BZ_ENODATA);   // no geometry
    CHECK(host->component_bounds(999, lo, hi) == BZ_ENOTFOUND);
    SetActiveAssembly(nullptr);
}

TEST_CASE("a module off by default waits for the user's consent") {
    Fixture f;
    f.Add("hello", BZ_EXAMPLE_MODULE);
    std::string manifest = ReadFile(f.mods / "hello" / "module.json");
    manifest.insert(manifest.find('{') + 1, R"("enabledByDefault": false, )");
    WriteFile(f.mods / "hello" / "module.json", manifest);
    {
        ModuleManager mm(f.state, HostApi());
        mm.Discover(f.mods, Roomy());
        mm.LoadAll();
        CHECK(mm.Find("hello")->state == ModuleState::Off);
        CHECK(mm.TurnOn("hello"));
    }
    {
        ModuleManager mm(f.state, HostApi());   // a new run: the consent is remembered
        mm.Discover(f.mods, Roomy());
        mm.LoadAll();
        CHECK(mm.Find("hello")->state == ModuleState::Loaded);
        CHECK(mm.TurnOff("hello"));
    }
    ModuleManager mm(f.state, HostApi());   // switched off again: off, not "on by consent"
    mm.Discover(f.mods, Roomy());
    mm.LoadAll();
    CHECK(mm.Find("hello")->state == ModuleState::Off);

    CHECK_FALSE(ParseManifest(R"({"id": "x", "name": "x", "version": "1", "description": "", "abiVersion": 1, "library": "x",
                                  "enabledByDefault": "no"})", "linux"));
}

TEST_CASE("modules run commands on the main thread when they ask") {
    const BzHostApi* host = HostApi();
    REQUIRE(BZ_HAS(host, execute));
    REQUIRE(BZ_HAS(host, config_dir));

    const char* reply = nullptr;
    SetCommandHandler({});
    REQUIRE(host->execute(R"({"command": "app.documents"})", &reply) == BZ_OK);
    CHECK(std::string(reply).find(R"("ok":false)") != std::string::npos);   // the program is not ready
    CHECK(host->execute(nullptr, &reply) == BZ_EARGUMENT);

    std::string seen;
    SetCommandHandler([&](std::string_view request) {
        seen = request;
        return std::string(R"({"ok":true,"result":1})");
    });
    REQUIRE(host->execute(R"({"command": "app.documents"})", &reply) == BZ_OK);
    CHECK(std::string(reply) == R"({"ok":true,"result":1})");
    CHECK(seen == R"({"command": "app.documents"})");

    SetConfigDir("/home/me/.config/breezecad");
    CHECK(std::string(host->config_dir()) == "/home/me/.config/breezecad");

    int wakes = 0;
    SetWakeHandler([&] { ++wakes; });
    TakeMainRequest();
    host->request_main();
    CHECK(wakes == 1);
    CHECK(TakeMainRequest());
    CHECK_FALSE(TakeMainRequest());

    // The example asks for a main_tick in init and runs a command there.
    Fixture f;
    f.Add("hello", BZ_EXAMPLE_MODULE);
    ModuleManager mm(f.state, host);
    mm.Discover(f.mods, Roomy());
    seen.clear();
    mm.LoadAll();
    CHECK(mm.Tick());
    CHECK(seen.find("app.documents") != std::string::npos);
    CHECK_FALSE(mm.Tick());   // nobody asked again
    SetCommandHandler({});
    SetWakeHandler({});
}
