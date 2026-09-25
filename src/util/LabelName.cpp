#include "LabelName.h"

#include <TDataStd_Name.hxx>
#include <TCollection_AsciiString.hxx>

#include <sstream>

namespace BreezeCAD::Util {

std::string LabelKey(const TDF_Label& label) {
    std::ostringstream oss;
    label.EntryDump(oss);
    return oss.str();
}

std::string GetLabelName(const TDF_Label& label) {
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        return ascii.ToCString();
    }
    // Если имени нет — генерируем "Line <tag>"
    return "Line" + std::to_string(label.Tag());
}

} // namespace BreezeCAD::Util
