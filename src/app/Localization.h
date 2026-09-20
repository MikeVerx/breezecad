#pragma once

#include <string>

namespace BreezeCAD {

enum class Language { EN, RU };

class Localization {
public:
    static Localization& Instance();

    Language GetLanguage() const { return myLang; }
    void SetLanguage(Language lang) { myLang = lang; }
    void Toggle() { myLang = (myLang == Language::EN) ? Language::RU : Language::EN; }

    // Перевод по ключу. Если ключа нет — возвращает ключ как есть.
    std::string Tr(const std::string& key) const;

private:
    Localization() = default;
    Language myLang = Language::RU;  // по умолчанию — русский
};

// Удобный шорткат
inline std::string Tr(const std::string& key) {
    return Localization::Instance().Tr(key);
}

} // namespace BreezeCAD
