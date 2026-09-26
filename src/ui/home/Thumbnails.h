#pragma once
// ============================================================
// Thumbnails — превью документов и список недавних файлов.
// ============================================================

#include <string>
#include <vector>

namespace BreezeCAD { class OcctView; }

namespace BreezeCAD::UI::Home {

/// Путь к файлу превью документа или пустая строка.
std::string GetThumbnailPath(const std::vector<std::string>& thumbs, int docIndex);

/// Добавить файл в начало списка недавних (без дублей, max 10).
void AddRecentFile(std::vector<std::string>& recent, const std::string& path);

/// Снять превью активного документа (PNG/BMP) в кэш.
void CaptureCurrentThumbnail(OcctView& view, int activeDocIndex,
                             std::vector<std::string>& thumbs);

} // namespace BreezeCAD::UI::Home
