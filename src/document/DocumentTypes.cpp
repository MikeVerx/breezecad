#include "document/DocumentTypes.h"

#include <algorithm>

namespace bz::doc {

DocumentTypeRegistry& DocumentTypeRegistry::Default() {
    static DocumentTypeRegistry registry = [] {
        DocumentTypeRegistry r;
        r.Register({.id = std::string(kPartType), .title = "Деталь", .untitledBase = "Деталь", .group = "Детали", .icon = "tree-part"});
        // Its own extension, so assemblies stand out among part files; the
        // content says what it is, so a renamed file still opens.
        r.Register({.id = std::string(kAssemblyType), .title = "Сборка", .untitledBase = "Сборка", .extension = ".bza",
                    .group = "Сборки", .icon = "assembly"});
        r.Register({.id = std::string(kDrawingType), .title = "Чертёж", .untitledBase = "Чертёж",
                    .canCreate = false, .canOpen = false, .group = "Чертежи", .icon = "doc-drawing"});
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

std::string DocumentTypeRegistry::OpenPatterns() const {
    std::string out;
    for (const DocumentTypeInfo& t : myTypes) {
        if (!t.canOpen) continue;
        const std::string pattern = "*" + t.extension;
        if (out.find(pattern) != std::string::npos) continue;
        if (!out.empty()) out += ' ';
        out += pattern;
    }
    return out;
}

const DocumentTypeInfo* DocumentTypeRegistry::Find(std::string_view id) const {
    for (const DocumentTypeInfo& t : myTypes)
        if (t.id == id) return &t;
    return nullptr;
}

} // namespace bz::doc
