#pragma once
// What the "Создать" window offers: folders (Детали, Сборки, Чертежи…) with
// the kinds of documents from doc::DocumentTypeRegistry, each as one or more
// templates (a part in millimetres or in inches). A kind registered later
// (by a module) shows up here without changes to the window.
// No ImGui or OCCT here: tested on its own.

#include "core/Units.h"
#include "document/DocumentTypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bz::app {

struct NewTemplate {
    std::string               id;      ///< stable: "part.mm", "part.inch", "assembly"; remembered in the settings
    std::string               type;    ///< doc::DocumentTypeInfo::id
    std::string               title;   ///< Russian, translated when shown: "Деталь"
    std::string               note;    ///< "мм", "дюймы"; may be empty
    std::string               icon;
    bool                      available = true;   ///< false: shown grey, "позже"
    std::optional<LengthUnit> length;  ///< units of the new document, when the template sets them
};

struct NewGroup {
    std::string              name;        ///< Russian, translated when shown: "Детали"
    std::vector<NewTemplate> templates;
    /// Something in it can be created now (else the folder is grey, "позже").
    bool Available() const;
};

/// Folders in the order their first kind was registered.
std::vector<NewGroup> NewDocumentGroups(const doc::DocumentTypeRegistry& registry);

/// The template with `id`, or nullptr.
const NewTemplate* FindTemplate(const std::vector<NewGroup>& groups, std::string_view id);

/// What the window starts on: the last one used if it is still offered,
/// otherwise a part in `defaultLength` units (or the first available).
const NewTemplate* InitialTemplate(const std::vector<NewGroup>& groups, std::string_view last, LengthUnit defaultLength);

} // namespace bz::app
