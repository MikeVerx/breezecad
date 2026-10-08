#pragma once
// The .bzc file: a ZIP archive with JSON inside. Specification:
// docs/FILE_FORMAT.md. The recipe (features and their inputs) is the source
// of truth; anything computed is only a cache and may be absent.

#include "core/Expected.h"
#include "document/Document.h"

#include <filesystem>
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

/// Writes the document atomically: a temporary file is written next to the
/// target and renamed over it, so a crash never leaves a half-written file.
Status SaveBzc(const Document& doc, const std::filesystem::path& path, std::string_view generator);

Expected<Document> LoadBzc(const std::filesystem::path& path, LoadReport* report = nullptr);

/// The document part of the archive as JSON text (also used by tests).
std::string        DocumentToJson(const Document& doc);
Expected<Document> DocumentFromJson(std::string_view json, LoadReport* report = nullptr);

} // namespace bz::doc
