#pragma once

#include <string>

namespace OrdoCAD {

// Открыть нативный диалог выбора файла.
//   save = false → "Открыть файл"
//   save = true  → "Сохранить файл как"
// filter — фильтр в формате WinAPI: "OCAF (*.cbf)\0*.cbf\0All (*.*)\0*.*\0\0"
// Возвращает true, если пользователь выбрал файл; путь пишется в outPath.
bool ShowFileDialog(bool save, std::string& outPath,
                    const char* title = nullptr,
                    const char* filter = "OCAF Binary (*.cbf)\0*.cbf\0All Files (*.*)\0*.*\0\0",
                    const char* defaultExt = "cbf");

} // namespace OrdoCAD
