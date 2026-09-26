#pragma once
// ============================================================
// LabelName — утилиты работы с TDF_Label (OCAF).
//
//   LabelKey(label)      — строковый ключ для map (EntryDump).
//   GetLabelName(label)  — имя из TDataStd_Name или "Line<tag>".
//
// Известное ограничение: GetLabelName использует AsciiString,
// поэтому кириллические имена теряются. См. BUG в TECH_DEBT.
// ============================================================

#include <string>
#include <TDF_Label.hxx>

namespace BreezeCAD::Util {

/// Уникальный строковый ключ метки (для std::map / кэшей).
std::string LabelKey(const TDF_Label& label);

/// Человекочитаемое имя метки. Если атрибут имени отсутствует —
/// возвращает "Line<tag>".
std::string GetLabelName(const TDF_Label& label);

} // namespace BreezeCAD::Util
