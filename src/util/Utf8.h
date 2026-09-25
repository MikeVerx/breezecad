#pragma once
// ============================================================
// Utf8 — конвертеры между UTF-8 (std::string) и
// TCollection_ExtendedString (UTF-16, OCCT).
//
// Раньше эти функции дублировались в Application.cpp и
// BreezeDocument.cpp. Теперь единая точка.
// ============================================================

#include <string>
#include <TCollection_ExtendedString.hxx>

namespace BreezeCAD::Util {

TCollection_ExtendedString Utf8ToExt(const std::string& s);
std::string ExtToUtf8(const TCollection_ExtendedString& es);

} // namespace BreezeCAD::Util
