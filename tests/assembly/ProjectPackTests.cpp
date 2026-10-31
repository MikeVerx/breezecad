// .bzp: an assembly with all its parts in one file, unpacked whole elsewhere.
#include "../Fixtures.h"

#include "assembly/AssemblyFile.h"
#include "assembly/ProjectPack.h"
#include "core/PathText.h"
#include "document/BzcFile.h"

#include <doctest.h>
#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <set>

using namespace bz;
using namespace bz::assembly;
namespace fs = std::filesystem;

namespace {

fs::path Fresh(const char* name) {
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8(name);
    std::error_code ec;
    fs::remove_all(dir, ec);   // a leftover of a run that failed half way
    fs::create_directories(dir);
    return dir;
}

void WritePart(const fs::path& file, int rectangles = 1) {
    doc::Document d;
    for (int i = 0; i < rectangles; ++i) {
        doc::SketchFeature& sk = test::AddSketch(d);
        test::AddRect(d, sk, {0, 0}, {10.0 + i, 10});
    }
    fs::create_directories(file.parent_path());
    REQUIRE(doc::SaveBzc(d, file, "test"));
}

std::set<std::string> Entries(const fs::path& zip) {
    std::ifstream in(zip, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(in), {}};
    mz_zip_archive z{};
    REQUIRE(mz_zip_reader_init_mem(&z, bytes.data(), bytes.size(), 0));
    std::set<std::string> out;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); ++i) {
        char name[512];
        mz_zip_reader_get_filename(&z, i, name, sizeof(name));
        out.insert(name);
    }
    mz_zip_reader_end(&z);
    return out;
}

/// A project with every kind of part reference: beside the assembly, in a
/// subfolder, outside its folder, outside with a name that clashes, twice
/// the same file, and one that is missing.
fs::path SampleProject(const fs::path& root) {
    const fs::path dir = root / PathFromUtf8("Проект");
    WritePart(dir / PathFromUtf8("Основание.bzc"));
    WritePart(dir / PathFromUtf8("Стандартные изделия") / PathFromUtf8("Винт М4.bzc"));
    WritePart(root / PathFromUtf8("Общие") / PathFromUtf8("Ось.bzc"));
    WritePart(root / PathFromUtf8("Другие") / PathFromUtf8("Основание.bzc"), 2);   // same name, another part
    const fs::path file = dir / PathFromUtf8("Узел.bza");
    Assembly a;
    a.info = {"Узел", "БРЗ.200.000"};
    // Names get their number from AddComponent: "Основание:1", "Основание:2"...
    a.AddComponent("Основание.bzc", "Основание");
    a.AddComponent("Основание.bzc", "Основание");
    a.AddComponent("Стандартные изделия/Винт М4.bzc", "Винт");
    a.AddComponent("../Общие/Ось.bzc", "Ось");
    a.AddComponent(ToUtf8(root / PathFromUtf8("Другие") / PathFromUtf8("Основание.bzc")), "Чужое основание");
    a.AddComponent("Пропала.bzc", "Пропала");
    REQUIRE(SaveAssembly(a, file, "test"));
    return file;
}

} // namespace

TEST_CASE("a project carries the assembly and every part, and opens whole somewhere else") {
    const fs::path root = Fresh("bz-проект-пакет");
    const fs::path assemblyFile = SampleProject(root);
    const fs::path bzp = root / PathFromUtf8("Узел.bzp");

    const auto packed = PackProject(assemblyFile, bzp, "0.2.5-Beta");
    REQUIRE(packed);
    CHECK(packed->parts == 4);   // the shared one counted once, the missing one not at all
    REQUIRE(packed->missing.size() == 1);
    CHECK(packed->missing[0] == "Пропала:1");

    const std::set<std::string> entries = Entries(bzp);
    CHECK(entries.contains("project.json"));
    CHECK(entries.contains("Узел.bza"));
    CHECK(entries.contains("Основание.bzc"));
    CHECK(entries.contains("Стандартные изделия/Винт М4.bzc"));   // kept where it was
    CHECK(entries.contains("Ось.bzc"));                           // from outside: next to the assembly
    CHECK(entries.contains("Основание (2).bzc"));                 // the clashing name renamed
    CHECK(entries.size() == 6);
    for (const std::string& e : entries) CHECK_MESSAGE(e.find("..") == std::string::npos, e);

    const auto manifest = ReadProject(bzp);
    REQUIRE(manifest);
    CHECK(manifest->main == "Узел.bza");
    CHECK(manifest->name == "Узел");
    CHECK(manifest->createdBy == "0.2.5-Beta");
    CHECK(manifest->files.size() == 5);

    // Elsewhere: every component finds its part, the original is untouched.
    const fs::path target = root / PathFromUtf8("Распаковано") / PathFromUtf8("Узел");
    const auto main = UnpackProject(bzp, target);
    REQUIRE(main);
    CHECK(*main == target / PathFromUtf8("Узел.bza"));
    const auto opened = LoadAssembly(*main);
    REQUIRE(opened);
    CHECK(opened->info.designation == "БРЗ.200.000");
    int found = 0;
    for (const Component& c : opened->components) {
        if (c.name == "Пропала:1") continue;
        CHECK_MESSAGE(fs::exists(ResolvePartPath(*main, c.path)), c.name);
        ++found;
    }
    CHECK(found == 5);
    // The two "Основание.bzc" are still two different parts.
    const Component* own = nullptr;
    const Component* other = nullptr;
    for (const Component& c : opened->components) {
        if (c.name == "Основание:1") own = &c;
        if (c.name == "Чужое основание:1") other = &c;
    }
    REQUIRE(own);
    REQUIRE(other);
    CHECK(own->path != other->path);
    const auto ownPart = doc::LoadBzc(ResolvePartPath(*main, own->path));
    const auto otherPart = doc::LoadBzc(ResolvePartPath(*main, other->path));
    REQUIRE(ownPart);
    REQUIRE(otherPart);
    CHECK(ownPart->FeatureCount() != otherPart->FeatureCount());
    // The source project is as it was.
    const auto source = LoadAssembly(assemblyFile);
    REQUIRE(source);
    CHECK(source->components[3].path == "../Общие/Ось.bzc");
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("unpacking never overwrites what is already in the folder") {
    const fs::path root = Fresh("bz-проект-без-перезаписи");
    const fs::path bzp = root / "p.bzp";
    REQUIRE(PackProject(SampleProject(root), bzp, "test"));
    const fs::path target = root / "out";
    fs::create_directories(target);
    std::ofstream(target / PathFromUtf8("Ось.bzc")) << "моя важная деталь";
    const auto main = UnpackProject(bzp, target);
    CHECK_FALSE(main);
    CHECK(main.GetError().message.find("Ось.bzc") != std::string::npos);
    // Nothing was written: not even the files that did not clash.
    CHECK_FALSE(fs::exists(target / PathFromUtf8("Узел.bza")));
    std::string kept;
    {
        // Closed before the folder goes: Windows does not delete an open file.
        std::ifstream in(target / PathFromUtf8("Ось.bzc"));
        std::getline(in, kept);
    }
    CHECK(kept == "моя важная деталь");
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("a project file that is not one, or is damaged or unsafe, is refused") {
    const fs::path root = Fresh("bz-проект-плохой");
    // Not a ZIP.
    std::ofstream(root / "text.bzp") << "hello";
    CHECK_FALSE(ReadProject(root / "text.bzp"));
    // A ZIP without the instruction (a part renamed .bzp).
    WritePart(root / "part.bzc");
    fs::copy_file(root / "part.bzc", root / "part.bzp");
    CHECK_FALSE(ReadProject(root / "part.bzp"));
    // An instruction that points outside the folder.
    const auto make = [&](const char* name, const std::string& json) {
        mz_zip_archive z{};
        REQUIRE(mz_zip_writer_init_heap(&z, 0, 1024));
        REQUIRE(mz_zip_writer_add_mem(&z, "project.json", json.data(), json.size(), MZ_DEFAULT_COMPRESSION));
        void* buf = nullptr;
        std::size_t size = 0;
        REQUIRE(mz_zip_writer_finalize_heap_archive(&z, &buf, &size));
        std::ofstream(root / name, std::ios::binary).write(static_cast<const char*>(buf), static_cast<std::streamsize>(size));
        mz_zip_writer_end(&z);
    };
    make("evil.bzp", R"({"format":"bzp","version":1,"main":"a.bza","files":[{"path":"../../evil.bzc","role":"part"}]})");
    CHECK_FALSE(ReadProject(root / "evil.bzp"));
    CHECK_FALSE(UnpackProject(root / "evil.bzp", root / "out"));
    CHECK_FALSE(fs::exists(root.parent_path() / "evil.bzc"));
    make("abs.bzp", R"({"format":"bzp","version":1,"main":"/etc/passwd","files":[]})");
    CHECK_FALSE(ReadProject(root / "abs.bzp"));
    make("other.bzp", R"({"format":"something","main":"a.bza"})");
    CHECK_FALSE(ReadProject(root / "other.bzp"));
    make("broken.bzp", "{not json");
    CHECK_FALSE(ReadProject(root / "broken.bzp"));
    // A file the instruction names but the archive lacks.
    make("short.bzp", R"({"format":"bzp","version":1,"main":"a.bza","files":[{"path":"a.bza","role":"assembly"}]})");
    CHECK(ReadProject(root / "short.bzp"));
    CHECK_FALSE(UnpackProject(root / "short.bzp", root / "out2"));
    CHECK_FALSE(ReadProject(root / "no-such.bzp"));
    std::error_code ec;
    fs::remove_all(root, ec);
}
