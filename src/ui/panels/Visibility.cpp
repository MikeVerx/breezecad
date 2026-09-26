#include "Visibility.h"
#include "doc/BreezeDocument.h"
#include "occt/OcctView.h"

#include <iostream>
#include <vector>

namespace BreezeCAD::UI::Panels {

bool IsLabelVisibleByDefault(const TDF_Label& label, bool hasOps) {
    if (label.IsNull()) return false;

    int type = BreezeDocument::GetType(label);

    // Скрываемые системные элементы
    bool isSystemHideable = (type == FT_Axis || type == FT_Plane);
    if (isSystemHideable && hasOps) return false;

    return true;
}

void ShowAllRespectingDefaults(OcctView* view,
                                BreezeDocument* doc,
                                std::map<std::string, bool>& visibility,
                                std::map<std::string, bool>& isolationBackup,
                                bool& isolationActive) {
    if (!view || !doc) return;

    // Сбрасываем карту видимости — она была навязана изоляцией
    visibility.clear();
    isolationBackup.clear();
    isolationActive = false;

    bool hasOps = doc->Has3DOperations();

    // Проходим по всем объектам и применяем правила видимости
    auto allLabels = view->GetAllLabels();
    for (const auto& lbl : allLabels) {
        bool visible = IsLabelVisibleByDefault(lbl, hasOps);
        view->SetShapeVisible(lbl, visible);
    }

    std::cout << "ShowAllRespectingDefaults: restored\n";
}

} // namespace BreezeCAD::UI::Panels
