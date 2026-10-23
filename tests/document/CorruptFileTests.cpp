// Damaged or hand-edited files must be rejected with an error, never crash
// (audit 2026-10-13, B-01). The JSON samples are those of tests/audit/robustness_probes.
#include "assembly/AssemblyFile.h"
#include "document/BzcFile.h"

#include <doctest.h>
#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace bz;

namespace {

// Loading must not throw; the result itself may be an error or a document.
void LoadsWithoutThrowing(const std::string& json) {
    CAPTURE(json);
    CHECK_NOTHROW((void)doc::DocumentFromJson(json));
}

std::filesystem::path WriteZip(const std::string& name, const std::string& manifest, const std::string& document) {
    const auto path = std::filesystem::temp_directory_path() / name;
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_heap(&zip, 0, 1024));
    REQUIRE(mz_zip_writer_add_mem(&zip, "manifest.json", manifest.data(), manifest.size(), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_add_mem(&zip, "document.json", document.data(), document.size(), MZ_DEFAULT_COMPRESSION));
    void* data = nullptr;
    std::size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
    std::ofstream(path, std::ios::binary).write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    mz_free(data);
    mz_zip_writer_end(&zip);
    return path;
}

} // namespace

TEST_CASE("B-01: a part with fields of the wrong type opens without them, never throws") {
    const char* samples[] = {
        R"({"part":{"name":5}})",
        R"({"units":"mm"})",
        R"({"features":[5]})",
        R"({"features":[{"type":7,"id":1}]})",
        R"({"nextId":"9"})",
    };
    for (const char* json : samples) {
        CAPTURE(json);
        LoadsWithoutThrowing(json);
        doc::LoadReport report;
        const auto d = doc::DocumentFromJson(json, &report);
        REQUIRE(d);   // the damaged entry is dropped, the part opens
        CHECK(d->FeatureCount() == 0);
    }
    // Duplicate and out-of-range ids: the second of a pair is dropped; ids
    // handed out later never overflow.
    doc::LoadReport report;
    auto d = doc::DocumentFromJson(R"({"features":[{"type":"workplane","id":3,"name":"A"},{"type":"workplane","id":3,"name":"B"},
        {"type":"workplane","id":18446744073709551615,"name":"C"}]})", &report);
    REQUIRE(d);
    CHECK(d->FeatureCount() == 1);
    CHECK(report.warnings.size() == 2);
    const std::uint64_t next = d->Ids().Next().value;
    CHECK(next > 3);
    CHECK(next < 1000);
    // Harmless damage still loads as before.
    LoadsWithoutThrowing(R"({"variables":{"a":1}})");
    LoadsWithoutThrowing(R"({"features":[{"type":"sketch","id":1,"name":"s","points":5}]})");
}

TEST_CASE("damage inside one operation keeps it as it was and opens the rest of the part") {
    const std::string json = R"({"features":[
        {"type":"sketch","id":1,"name":"Эскиз1","curves":[{"type":3}]},
        {"type":"workplane","id":2,"name":"Плоскость1"}]})";
    LoadsWithoutThrowing(json);
    doc::LoadReport report;
    auto loaded = doc::DocumentFromJson(json, &report);
    REQUIRE(loaded);
    REQUIRE(loaded->FeatureCount() == 2);
    const doc::Feature* damaged = loaded->Find(ObjectId{1});
    REQUIRE(damaged);
    CHECK(damaged->Type() == "sketch");                       // saved back as a sketch
    CHECK(damaged->status.state == doc::FeatureState::Error);
    CHECK(damaged->name == "Эскиз1");
    CHECK_FALSE(report.warnings.empty());
    // Saved and opened again: the damaged fields are still there, unchanged.
    const std::string again = doc::DocumentToJson(*loaded);
    CHECK(again.find("\"curves\"") != std::string::npos);
    auto reloaded = doc::DocumentFromJson(again);
    REQUIRE(reloaded);
    CHECK(reloaded->FeatureCount() == 2);
}

TEST_CASE("B-01: an assembly with fields of the wrong type opens without them, never throws") {
    const char* samples[] = {
        R"({"assembly":{"name":5}})",
        R"({"components":5})",
    };
    for (const char* json : samples) {
        CAPTURE(json);
        CHECK_NOTHROW((void)assembly::AssemblyFromJson(json));
        const auto a = assembly::AssemblyFromJson(json);
        REQUIRE(a);
        CHECK(a->components.empty());
        CHECK(a->info.name.empty());
    }
}

TEST_CASE("B-01: a manifest with fields of the wrong type is rejected, not thrown") {
    const auto wrongFormat = WriteZip("bz_b01_format.bzc", R"({"format":5})", "{}");
    const auto wrongType   = WriteZip("bz_b01_type.bzc", R"({"format":"bzc","documentType":3})", "{}");
    const auto notObject   = WriteZip("bz_b01_array.bzc", R"([1,2])", "{}");
    for (const auto& path : {wrongFormat, wrongType, notObject}) {
        CAPTURE(path.string());
        CHECK_NOTHROW((void)doc::ReadBzcArchive(path));
        CHECK_FALSE(doc::ReadBzcArchive(path));
        CHECK_NOTHROW((void)doc::LoadBzc(path));
        std::filesystem::remove(path);
    }
}
