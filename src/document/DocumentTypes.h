#pragma once
// Kinds of documents BreezeCAD knows: a part, an assembly, later a drawing
// (and whatever a module adds). Every .bzc file names its kind in the
// manifest ("documentType"); the program looks the kind up here instead of
// assuming "part" in its code, so a new kind is one Register() call.

#include <string>
#include <string_view>
#include <vector>

namespace bz::doc {

struct DocumentTypeInfo {
    std::string id;            ///< in the manifest: "part", "assembly", "drawing"
    std::string title;         ///< "Деталь" (translated by the UI when shown)
    std::string untitledBase;  ///< base of new names: "Деталь" -> "Деталь1"
    std::string extension = ".bzc";
    bool        canCreate = true;   ///< a "Создать" button is offered
    bool        canOpen   = true;   ///< this version can open such files
};

class DocumentTypeRegistry {
public:
    /// Part, assembly and drawing (the drawing is a placeholder: it can
    /// be neither created nor opened until its module exists).
    static DocumentTypeRegistry& Default();

    /// Adds a kind, or replaces the one with the same id.
    void Register(DocumentTypeInfo info);
    const DocumentTypeInfo* Find(std::string_view id) const;
    const std::vector<DocumentTypeInfo>& All() const { return myTypes; }

private:
    std::vector<DocumentTypeInfo> myTypes;
};

inline constexpr std::string_view kPartType     = "part";
inline constexpr std::string_view kAssemblyType = "assembly";
inline constexpr std::string_view kDrawingType  = "drawing";

} // namespace bz::doc
