#include "app/Localization.h"

#include <unordered_map>

namespace OrdoCAD {

namespace {

// Ключ -> {EN, RU}
const std::unordered_map<std::string, std::pair<std::string, std::string>> kDict = {
    // Панель дерева
    { "panel.title",           { "OCAF Document Tree",         "Дерево документа OCAF" } },
    { "panel.hint.delete",     { "RMB on node - delete, Delete key - remove selected",
                                 "ПКМ по узлу — удалить, Delete — удалить выбранное" } },
    { "panel.document",        { "Document (BinOcaf)",         "Документ (BinOcaf)" } },
    { "panel.document.null",   { "Document: NULL",             "Документ: NULL" } },

    // Кнопки
    { "btn.addBox",            { "Add Box",                    "Добавить куб" } },
    { "btn.fullscreen",        { "Fullscreen (F11)",           "На весь экран (F11)" } },
    { "btn.exitFullscreen",    { "Exit Fullscreen (F11)",      "Выйти из полного экрана (F11)" } },
    { "btn.maximize",          { "Maximize",                   "Развернуть" } },
    { "btn.ok",                { "OK",                         "ОК" } },
    { "btn.cancel",            { "Cancel",                     "Отмена" } },

    // Диалог Add Box
    { "dlg.addBox.title",      { "Add Box",                    "Добавить куб" } },
    { "dlg.name",              { "Name",                       "Имя" } },
    { "dlg.position",          { "Position (corner):",         "Позиция (угол):" } },
    { "dlg.size",              { "Size:",                      "Размеры:" } },

    // Debug-панель
    { "debug.title",           { "OrdoCAD Debug",              "OrdoCAD — отладка" } },
    { "debug.controls",        { "Controls:",                  "Управление:" } },
    { "debug.lmb",             { "LMB - select",               "ЛКМ — выбрать" } },
    { "debug.rmb",             { "RMB - rotate",               "ПКМ — вращение" } },
    { "debug.mmb",             { "MMB - pan",                  "СКМ — панорама" } },
    { "debug.wheel",           { "Wheel - zoom",               "Колесо — зум" } },
    { "debug.delete",          { "Delete - remove selected",   "Delete — удалить выбранное" } },
    { "debug.f11",             { "F11 - fullscreen",           "F11 — полный экран" } },
    { "debug.view.active",     { "OCCT view: active",          "3D-вид: активен" } },
    { "debug.view.inactive",   { "OCCT view: NOT initialized", "3D-вид: не инициализирован" } },
    { "debug.selected.tag",    { "Selected: tag=%d",           "Выбрано: тег=%d" } },
    { "debug.selected.none",   { "Selected: none",             "Выбрано: ничего" } },

    // Язык
    { "lang.label",            { "Language:",                  "Язык:" } },
    { "lang.en",               { "English",                    "English" } },
    { "lang.ru",               { "Русский",                    "Русский" } },

    // Значения по умолчанию
    { "default.boxName",       { "Box",                        "Куб" } },
    { "default.cylinderName",  { "Cylinder",                   "Цилиндр" } },
    { "default.holeName",      { "Hole",                       "Отверстие" } },
};

} // namespace

Localization& Localization::Instance() {
    static Localization instance;
    return instance;
}

std::string Localization::Tr(const std::string& key) const {
    auto it = kDict.find(key);
    if (it == kDict.end()) return key;
    return (myLang == Language::EN) ? it->second.first : it->second.second;
}

} // namespace OrdoCAD
