#include "document/BzcFile.h"

#include "core/PathText.h"
#include "document/DocumentTypes.h"
#include "document/FeatureRegistry.h"
#include "document/JsonIO.h"
#include "document/UnknownFeature.h"

#include <miniz.h>

#include <cassert>

#include <chrono>
#include <format>
#include <map>
#include <fstream>
#include <iterator>
#include <sstream>

namespace bz::doc {
namespace {

using json_io::Json;
using json_io::ReadMember;

constexpr const char* kManifestName = "manifest.json";
constexpr const char* kDocumentName = "document.json";

std::string NowIso() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

Json Manifest(std::string_view documentType, std::string_view generator) {
    return Json{
        {"format", "bzc"},
        {"formatVersion", kBzcFormatVersion},
        {"documentType", documentType},
        {"generator", generator},
        {"modified", NowIso()},
        {"internalUnits", {{"length", "mm"}, {"angle", "rad"}}},
        {"content", {{"document", kDocumentName}}},
    };
}

Expected<std::string> ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{"Не удалось открыть файл"};
    return std::string(std::istreambuf_iterator<char>(in), {});
}

/// Extracts one entry of an in-memory ZIP. Empty optional-like error when missing.
Expected<std::string> ExtractEntry(mz_zip_archive& zip, const char* name) {
    const int index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    if (index < 0) return Error{std::format("В архиве нет {}", name)};
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0);
    if (!data) return Error{std::format("Не удалось распаковать {}", name)};
    std::string text(static_cast<const char*>(data), size);
    mz_free(data);
    return text;
}

} // namespace

std::string DocumentToJson(const Document& doc) {
    Json variables = Json::array();
    for (const Parameter& v : doc.variables) variables.push_back(json_io::ToJson(v));

    Json features = Json::array();
    for (const auto& f : doc.Features()) {
        Json j{{"id", f->id.value}, {"type", f->Type()}, {"name", f->name}};
        if (f->suppressed) j["suppressed"] = true;
        if (f->hidden) j["hidden"] = true;   // absent in older files: shown
        // A feature's own fields must not reuse the common keys (a hole once
        // wrote its kind as "type" and could not be opened again).
        Json fields = Json::object();
        f->SaveFields(fields);
        for (const char* reserved : {"id", "type", "name", "suppressed", "hidden"})
            assert(!fields.contains(reserved) && "SaveFields wrote a reserved key");
        j.update(fields);
        features.push_back(std::move(j));
    }

    Json root{
        {"part", {{"name", doc.info.name}, {"designation", doc.info.designation}, {"material", doc.info.material}}},
        {"units", {{"length", ToId(doc.units.length)}, {"angle", ToId(doc.units.angle)}}},
        {"nextId", doc.Ids().Last() + 1},
        {"variables", std::move(variables)},
        {"features", std::move(features)},
    };
    if (doc.EndMarker() < doc.FeatureCount()) root["endMarker"] = doc.EndMarker();
    if (!doc.look.Empty()) root["look"] = ToJson(doc.look);
    return root.dump(1, '\t');
}

Expected<Document> DocumentFromJson(std::string_view text, LoadReport* report) {
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return Error{"Содержимое документа повреждено (не JSON)"};

    Document doc;
    if (const auto part = root.find("part"); part != root.end() && part->is_object()) {
        doc.info.name        = part->value("name", "");
        doc.info.designation = part->value("designation", "");
        doc.info.material    = part->value("material", "");
    }
    if (const auto units = root.find("units"); units != root.end()) {
        if (auto l = LengthUnitFromId(units->value("length", "mm"))) doc.units.length = *l;
        if (auto a = AngleUnitFromId(units->value("angle", "deg"))) doc.units.angle = *a;
    }
    for (const Json& v : root.value("variables", Json::array())) {
        Parameter p;
        if (!json_io::Read(v, p)) return Error{"Таблица переменных повреждена"};
        doc.variables.push_back(std::move(p));
    }

    const FeatureRegistry& registry = FeatureRegistry::Default();
    for (const Json& j : root.value("features", Json::array())) {
        const std::string type = j.value("type", "");
        std::unique_ptr<Feature> feature = registry.Create(type);
        if (!feature) {
            auto unknown = std::make_unique<UnknownFeature>();
            unknown->typeId = type;
            unknown->status = {FeatureState::Error, std::format("Тип операции «{}» не поддерживается этой версией", type)};
            if (report) report->warnings.push_back(std::format("Операция типа «{}» сохранена, но не будет построена", type));
            feature = std::move(unknown);
        }
        if (!ReadMember(j, "id", feature->id) || !feature->id || !ReadMember(j, "name", feature->name) ||
            !ReadMember(j, "suppressed", feature->suppressed) || !ReadMember(j, "hidden", feature->hidden))
            return Error{"Повреждена операция в дереве"};
        if (Status s = feature->LoadFields(j); !s) return s.GetError();
        doc.Insert(std::move(feature), doc.FeatureCount());
    }

    if (std::uint64_t next = root.value("nextId", std::uint64_t{0}); next > 0) doc.Ids().Reserve(ObjectId{next - 1});
    // Files written by other programs may lack "nextId": make sure new ids
    // never collide with ids inside features (sketch points, constraints).
    for (const auto& f : doc.Features()) f->ReserveIds(doc.Ids());
    if (const auto marker = root.find("endMarker"); marker != root.end() && marker->is_number_unsigned())
        doc.SetEndMarker(marker->get<std::size_t>());

    if (const auto look = root.find("look"); look != root.end()) doc.look = LookFromJson(*look);

    doc.ClearDirty();
    doc.MarkChanged(0);   // everything needs a first rebuild
    return doc;
}

Status WriteBzcArchive(const std::filesystem::path& path, std::string_view documentType, std::string_view generator,
                       const std::string& document, const std::map<std::string, std::vector<std::uint8_t>>& pictures) {
    const std::string manifest = Manifest(documentType, generator).dump(1, '\t');

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) return Error{"Не удалось подготовить архив"};
    bool added =
        mz_zip_writer_add_mem(&zip, kManifestName, manifest.data(), manifest.size(), MZ_BEST_COMPRESSION) &&
        mz_zip_writer_add_mem(&zip, kDocumentName, document.data(), document.size(), MZ_BEST_COMPRESSION);
    // Pictures of the appearances (already compressed formats: stored as they are).
    for (const auto& [name, data] : pictures)
        added = added && mz_zip_writer_add_mem(&zip, ("pictures/" + name).c_str(), data.data(), data.size(), MZ_NO_COMPRESSION);
    void* buffer = nullptr;
    std::size_t size = 0;
    const bool finalized = added && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    std::string bytes = finalized ? std::string(static_cast<const char*>(buffer), size) : std::string{};
    mz_zip_writer_end(&zip);
    if (!finalized) return Error{"Не удалось упаковать документ"};

    std::filesystem::path temp = path;
    temp += ".saving";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return Error{std::format("Нет доступа к папке: {}", ToUtf8(path.parent_path()))};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) return Error{"Не удалось записать файл (диск заполнен?)"};
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return Error{std::format("Не удалось заменить файл: {}", ToUtf8(path))};
    }
    return Status::Ok();
}

Expected<BzcArchive> ReadBzcArchive(const std::filesystem::path& path) {
    Expected<std::string> bytes = ReadWholeFile(path);
    if (!bytes) return bytes.GetError();

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes->data(), bytes->size(), 0))
        return Error{"Файл не является документом BreezeCAD (.bzc)"};

    Expected<std::string> manifestText = ExtractEntry(zip, kManifestName);
    Expected<std::string> documentText = ExtractEntry(zip, kDocumentName);
    BzcArchive archive;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        const std::string entry = name;
        if (!entry.starts_with("pictures/") || entry.size() <= 9) continue;
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) continue;
        const auto* bytesOf = static_cast<const std::uint8_t*>(data);
        archive.pictures[entry.substr(9)] = std::vector<std::uint8_t>(bytesOf, bytesOf + size);
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    if (!manifestText) return manifestText.GetError();
    if (!documentText) return documentText.GetError();

    const Json manifest = Json::parse(*manifestText, nullptr, false);
    if (manifest.is_discarded() || manifest.value("format", "") != "bzc")
        return Error{"Файл не является документом BreezeCAD (.bzc)"};
    archive.formatVersion = manifest.value("formatVersion", 0);
    archive.documentType  = manifest.value("documentType", std::string(kPartType));
    archive.documentJson  = std::move(*documentText);
    return archive;
}

Expected<std::string> ReadDocumentType(const std::filesystem::path& path) {
    Expected<BzcArchive> archive = ReadBzcArchive(path);
    if (!archive) return archive.GetError();
    return archive->documentType;
}

Status SaveBzc(const Document& doc, const std::filesystem::path& path, std::string_view generator) {
    return WriteBzcArchive(path, kPartType, generator, DocumentToJson(doc), doc.look.pictures);
}

Expected<Document> LoadBzc(const std::filesystem::path& path, LoadReport* report) {
    Expected<BzcArchive> archive = ReadBzcArchive(path);
    if (!archive) return archive.GetError();
    if (archive->documentType != kPartType) {
        const DocumentTypeInfo* type = DocumentTypeRegistry::Default().Find(archive->documentType);
        return Error{std::format("Это не деталь, а документ «{}»", type ? type->title : archive->documentType)};
    }
    if (archive->formatVersion > kBzcFormatVersion && report)
        report->warnings.push_back("Файл создан более новой версией BreezeCAD. Новые возможности могут не открыться.");

    Expected<Document> doc = DocumentFromJson(archive->documentJson, report);
    if (doc) {
        doc->look.pictures = std::move(archive->pictures);
        doc->look.Prune();   // pictures nothing refers to
    }
    return doc;
}

} // namespace bz::doc
