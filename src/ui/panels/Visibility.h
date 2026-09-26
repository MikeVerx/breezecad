#pragma once
// ============================================================
// Visibility — логика видимости системных элементов (плоскости, оси).
//
// Единственная точка правил:
//   - по умолчанию плоскости/оси скрыты, если есть 3D-операции
//   - ShowAllRespectingDefaults сбрасывает изоляцию и восстанавливает
//     видимость по этим правилам
//
// Закрывает B-01 (тройное дублирование логики видимости).
// ============================================================

#include <map>
#include <string>
#include <TDF_Label.hxx>

namespace BreezeCAD {
class BreezeDocument;
class OcctView;
}

namespace BreezeCAD::UI::Panels {

/// Видима ли метка по умолчанию.
/// Системные (Plane/Axis) скрыты, если в документе есть 3D-операции.
bool IsLabelVisibleByDefault(const TDF_Label& label, bool hasOps);

/// Снять изоляцию и восстановить видимость по правилам по умолчанию.
/// Сбрасывает карту видимости полностью — она была навязана изоляцией.
void ShowAllRespectingDefaults(OcctView* view,
                                BreezeDocument* doc,
                                std::map<std::string, bool>& visibility,
                                std::map<std::string, bool>& isolationBackup,
                                bool& isolationActive);

} // namespace BreezeCAD::UI::Panels
