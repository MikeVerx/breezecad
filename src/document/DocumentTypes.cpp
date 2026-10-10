#include "document/DocumentTypes.h"

#include <algorithm>

namespace bz::doc {

DocumentTypeRegistry& DocumentTypeRegistry::Default() {
    static DocumentTypeRegistry registry = [] {
        DocumentTypeRegistry r;
        r.Register({.id = std::string(kPartType), .title = "Деталь", .untitledBase = "Деталь"});
        r.Register({.id = std::string(kAssemblyType), .title = "Сборка", .untitledBase = "Сборка"});
        r.Register({.id = std::string(kDrawingType), .title = "Чертёж", .untitledBase = "Чертёж",
                    .canCreate = false, .canOpen = false});
        return r;
    }();
    return registry;
}

void DocumentTypeRegistry::Register(DocumentTypeInfo info) {
    const auto it = std::find_if(myTypes.begin(), myTypes.end(), [&](const auto& t) { return t.id == info.id; });
    if (it != myTypes.end())
        *it = std::move(info);
    else
        myTypes.push_back(std::move(info));
}

const DocumentTypeInfo* DocumentTypeRegistry::Find(std::string_view id) const {
    for (const DocumentTypeInfo& t : myTypes)
        if (t.id == id) return &t;
    return nullptr;
}

} // namespace bz::doc
