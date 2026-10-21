// The parts go with the assembly (assembly::GatherParts): every part lies in
// the assembly's folder, a part from elsewhere is copied there as
// "<деталь> - <сборка>.bzc". An assembly made on a phone opened on a computer
// found none of its parts: they were referred to in the app's cache.
#include "assembly/AssemblyFile.h"
#include "core/PathText.h"
#include "document/BzcFile.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace bz;
using namespace bz::assembly;
namespace fs = std::filesystem;

namespace {

fs::path Root(const char* name) {
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8(name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void Write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

/// The desktop's folder: new files beside the assembly, written by GatherParts.
PartFolder Desktop(const fs::path& assemblyFile, bool subfolders = true) {
    PartFolder f;
    f.newFile = [assemblyFile](const std::string& name) {
        const fs::path folder = assemblyFile.parent_path();
        const fs::path wanted = PathFromUtf8(name);
        if (!fs::exists(folder / wanted)) return folder / wanted;
        for (int n = 2;; ++n)
            if (const fs::path c = folder / PathFromUtf8(ToUtf8(wanted.stem()) + " (" + std::to_string(n) + ")" + ToUtf8(wanted.extension()));
                !fs::exists(c))
                return c;
    };
    f.written = [](const fs::path&) { return true; };
    f.subfolders = subfolders;
    return f;
}

std::size_t FilesIn(const fs::path& dir) {
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file()) ++n;
    return n;
}

} // namespace

TEST_CASE("the copy's name: the part, a dash, the assembly") {
    CHECK(CopyName(PathFromUtf8("/lib/Вал.bzc"), PathFromUtf8("/proj/Узел.bza")) == "Вал - Узел.bzc");
    // A copy made for the assembly saved before under another name is named once.
    CHECK(CopyName(PathFromUtf8("/a/Вал - Узел.bzc"), PathFromUtf8("/b/Узел 2.bza"), PathFromUtf8("/a/Узел.bza")) == "Вал - Узел 2.bzc");
    CHECK(CopyName(PathFromUtf8("/a/Вал - Узел.bzc"), PathFromUtf8("/a/Узел.bza")) == "Вал - Узел.bzc");
}

TEST_CASE("a part from elsewhere is copied beside the assembly, one copy for equal components") {
    const fs::path root = Root("bz_рядом_копия");
    const fs::path lib = root / PathFromUtf8("Библиотека"), proj = root / PathFromUtf8("Проект");
    Write(lib / PathFromUtf8("Вал.bzc"), "вал");
    Write(lib / PathFromUtf8("Втулка.bzc"), "втулка");
    fs::create_directories(proj);
    const fs::path file = proj / PathFromUtf8("Узел.bza");

    Assembly a;   // a new assembly: absolute paths
    a.AddComponent(RelativePartPath({}, lib / PathFromUtf8("Вал.bzc")), "Вал");
    a.AddComponent(RelativePartPath({}, lib / PathFromUtf8("Вал.bzc")), "Вал");
    a.AddComponent(RelativePartPath({}, lib / PathFromUtf8("Втулка.bzc")), "Втулка");

    const GatherResult g = GatherParts(a, {}, file, Desktop(file));
    CHECK(g.ok);
    CHECK(g.problems.empty());
    CHECK(g.copied.size() == 2);
    CHECK(a.components[0].path == "Вал - Узел.bzc");
    CHECK(a.components[1].path == "Вал - Узел.bzc");
    CHECK(a.components[2].path == "Втулка - Узел.bzc");
    CHECK(Read(proj / PathFromUtf8("Вал - Узел.bzc")) == "вал");
    CHECK(FilesIn(proj) == 2);
    CHECK(Read(lib / PathFromUtf8("Вал.bzc")) == "вал");   // the original untouched

    // Saved again: nothing more to copy.
    const GatherResult again = GatherParts(a, file, file, Desktop(file));
    CHECK(again.copied.empty());
    CHECK(FilesIn(proj) == 2);
    fs::remove_all(root);
}

TEST_CASE("parts already in the assembly's folder stay where they are") {
    const fs::path root = Root("bz_рядом_уже");
    const fs::path file = root / PathFromUtf8("Узел.bza");
    Write(root / PathFromUtf8("Основание.bzc"), "основание");
    Write(root / PathFromUtf8("Стандартные изделия") / PathFromUtf8("Болт М8.bzc"), "болт");

    Assembly a;
    a.AddComponent("Основание.bzc", "Основание");
    a.AddComponent("Стандартные изделия/Болт М8.bzc", "Болт");
    const GatherResult g = GatherParts(a, file, file, Desktop(file));
    CHECK(g.copied.empty());
    CHECK(a.components[0].path == "Основание.bzc");
    CHECK(a.components[1].path == "Стандартные изделия/Болт М8.bzc");

    // A flat folder (Android): a part in a folder under it is copied up.
    const GatherResult flat = GatherParts(a, file, file, Desktop(file, false));
    CHECK(flat.ok);
    CHECK(a.components[1].path == "Болт М8 - Узел.bzc");
    CHECK(Read(root / PathFromUtf8("Болт М8 - Узел.bzc")) == "болт");
    fs::remove_all(root);
}

TEST_CASE("a name taken by another file gets a number; a copy with the same content is used again") {
    const fs::path root = Root("bz_рядом_имя");
    const fs::path lib = root / "lib", proj = root / "proj";
    const fs::path file = proj / PathFromUtf8("Узел.bza");
    Write(lib / PathFromUtf8("Вал.bzc"), "вал новый");
    Write(proj / PathFromUtf8("Вал - Узел.bzc"), "вал, исправленный в сборке");   // the user's own changes: kept

    Assembly a;
    a.AddComponent(RelativePartPath(file, lib / PathFromUtf8("Вал.bzc")), "Вал");
    GatherResult g = GatherParts(a, file, file, Desktop(file));
    REQUIRE(g.ok);
    CHECK(a.components[0].path == "Вал - Узел (2).bzc");
    CHECK(Read(proj / PathFromUtf8("Вал - Узел.bzc")) == "вал, исправленный в сборке");

    // The same part inserted again later: its copy is there with the same content.
    a.AddComponent(RelativePartPath(file, lib / PathFromUtf8("Вал.bzc")), "Вал");
    g = GatherParts(a, file, file, Desktop(file));
    CHECK(g.copied.empty());
    CHECK(a.components[1].path == "Вал - Узел (2).bzc");
    fs::remove_all(root);
}

TEST_CASE("save as in another folder takes the parts along") {
    const fs::path root = Root("bz_рядом_сохранить_как");
    const fs::path first = root / "a" / PathFromUtf8("Узел.bza"), second = root / "b" / PathFromUtf8("Узел 2.bza");
    Write(root / "a" / PathFromUtf8("Вал - Узел.bzc"), "вал");
    fs::create_directories(second.parent_path());

    Assembly a;
    a.AddComponent("Вал - Узел.bzc", "Вал");
    const GatherResult g = GatherParts(a, first, second, Desktop(second));
    CHECK(g.ok);
    CHECK(a.components[0].path == "Вал - Узел 2.bzc");
    CHECK(Read(root / "b" / PathFromUtf8("Вал - Узел 2.bzc")) == "вал");
    fs::remove_all(root);
}

TEST_CASE("a missing part is reported, not copied; a folder that takes nothing fails the save") {
    const fs::path root = Root("bz_рядом_нет");
    const fs::path file = root / "proj" / PathFromUtf8("Узел.bza");
    fs::create_directories(file.parent_path());
    Write(root / "lib" / PathFromUtf8("Вал.bzc"), "вал");

    Assembly a;
    a.AddComponent(RelativePartPath({}, root / "lib" / PathFromUtf8("Нет такой.bzc")), "Нет");
    GatherResult g = GatherParts(a, {}, file, Desktop(file));
    CHECK(g.ok);   // the rest is saved; the assembly shows the part missing
    REQUIRE(g.problems.size() == 1);
    CHECK(g.problems[0].find("не найдена") != std::string::npos);
    CHECK(ResolvePartPath(file, a.components[0].path) == (root / "lib" / PathFromUtf8("Нет такой.bzc")).lexically_normal());

    Assembly b;
    b.AddComponent(RelativePartPath({}, root / "lib" / PathFromUtf8("Вал.bzc")), "Вал");
    PartFolder none;   // Android outside the projects folder
    none.subfolders = false;
    g = GatherParts(b, {}, file, none);
    CHECK_FALSE(g.ok);
    // Still pointing at the part, from the new place.
    CHECK(ResolvePartPath(file, b.components[0].path) == (root / "lib" / PathFromUtf8("Вал.bzc")).lexically_normal());

    // A copy that does not reach its document (Android): a failure too.
    PartFolder lost = Desktop(file);
    lost.written = [](const fs::path&) { return false; };
    Assembly c;
    c.AddComponent(RelativePartPath({}, root / "lib" / PathFromUtf8("Вал.bzc")), "Вал");
    CHECK_FALSE(GatherParts(c, {}, file, lost).ok);
    fs::remove_all(root);
}

TEST_CASE("the assembly's folder carried elsewhere opens with all its parts") {
    // Made on the phone, the projects folder copied to a computer.
    const fs::path root = Root("bz_рядом_перенос");
    const fs::path phone = root / "phone", lib = root / "picked";
    Write(lib / PathFromUtf8("Вал.bzc"), "вал");
    REQUIRE(doc::SaveBzc(doc::Document{}, lib / PathFromUtf8("Корпус.bzc"), "tests"));
    const fs::path file = phone / PathFromUtf8("Редуктор.bza");
    fs::create_directories(phone);

    Assembly a;
    a.AddComponent(RelativePartPath({}, lib / PathFromUtf8("Вал.bzc")), "Вал");
    a.AddComponent(RelativePartPath({}, lib / PathFromUtf8("Корпус.bzc")), "Корпус");
    REQUIRE(GatherParts(a, {}, file, Desktop(file, false)).ok);
    REQUIRE(SaveAssembly(a, file, "tests"));

    const fs::path computer = root / "computer" / PathFromUtf8("Мои проекты");
    fs::create_directories(computer.parent_path());
    fs::copy(phone, computer, fs::copy_options::recursive);
    fs::remove_all(phone);
    fs::remove_all(lib);   // nothing left where the parts came from

    const fs::path moved = computer / PathFromUtf8("Редуктор.bza");
    const Expected<Assembly> opened = LoadAssembly(moved);
    REQUIRE(opened);
    for (const Component& c : opened->components) {
        CAPTURE(c.path);
        CHECK(fs::exists(ResolvePartPath(moved, c.path)));
    }
    CHECK(doc::LoadBzc(ResolvePartPath(moved, opened->components[1].path)));
    fs::remove_all(root);
}

TEST_CASE("an assembly recovered from the autosave finds its parts") {
    // The autosave writes the copy elsewhere, its paths made relative to that
    // place; recovering it puts the paths back relative to the original.
    const fs::path root = Root("bz_рядом_автосохранение");
    const fs::path original = root / "proj" / PathFromUtf8("Узел.bza");
    const fs::path copy = root / "config" / "autosave" / "7" / "copy1.bzc";
    Write(root / "proj" / PathFromUtf8("Вал - Узел.bzc"), "вал");

    Assembly a;
    a.AddComponent("Вал - Узел.bzc", "Вал");
    RebasePaths(a, original, copy);   // as Session::WriteFile does for the autosave
    CHECK(ResolvePartPath(original, a.components[0].path) != ResolvePartPath(copy, a.components[0].path));
    RebasePaths(a, copy, original);   // as RecoverDocument does now
    CHECK(a.components[0].path == "Вал - Узел.bzc");
    CHECK(fs::exists(ResolvePartPath(original, a.components[0].path)));
    fs::remove_all(root);
}
