#pragma once
// The .bzc file: a ZIP archive with JSON inside. Specification:
// docs/FILE_FORMAT.md. The recipe (features and their inputs) is the source
// of truth; anything computed is only a cache and may be absent.

#include "core/Expected.h"
#include "document/Document.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bz::doc {

inline constexpr int kBzcFormatVersion = 1;

struct LoadReport {
    /// Non-fatal problems: unknown feature types, newer format version...
    /// Shown to the user after opening.
    std::vector<std::string> warnings;
};

/// The container shared by every kind of document (DocumentTypes.h): a
/// manifest naming the kind, the document as JSON and its pictures.
struct BzcArchive {
    std::string documentType = "part";   ///< files without it are parts
    int         formatVersion = 0;
    std::string documentJson;
    std::map<std::string, std::vector<std::uint8_t>> pictures;   ///< by name, without "pictures/"
};

/// Reads the container; does not look inside the document.
Expected<BzcArchive> ReadBzcArchive(const std::filesystem::path& path);
/// Writes the container atomically (temporary file, then rename).
Status WriteBzcArchive(const std::filesystem::path& path, std::string_view documentType, std::string_view generator,
                       const std::string& documentJson,
                       const std::map<std::string, std::vector<std::uint8_t>>& pictures = {});
/// The kind of document in the file ("part", "assembly"...).
Expected<std::string> ReadDocumentType(const std::filesystem::path& path);

/// Writes the document atomically: a temporary file is written next to the
/// target and renamed over it, so a crash never leaves a half-written file.
Status SaveBzc(const Document& doc, const std::filesystem::path& path, std::string_view generator);

Expected<Document> LoadBzc(const std::filesystem::path& path, LoadReport* report = nullptr);

/// The document part of the archive as JSON text (also used by tests).
std::string        DocumentToJson(const Document& doc);
Expected<Document> DocumentFromJson(std::string_view json, LoadReport* report = nullptr);

} // namespace bz::doc
