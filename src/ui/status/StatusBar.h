#pragma once
// ============================================================
// StatusBar — нижняя полоса 24 px.
//
// Показывает: режим (эскиз/ожидание), состояние привязок,
// X/Y/Z курсора, zoom, статус. Не зависит от Application —
// принимает готовый StatusInfo.
// ============================================================

namespace BreezeCAD::UI::Status {

struct StatusInfo {
    bool   sketchActive       = false;
    bool   snapToGrid         = false;
    bool   snapToPoints       = false;
    double mouseX             = 0.0;
    double mouseY             = 0.0;
    bool   rightPanelExpanded = false;
};

void DrawStatusBar(const StatusInfo& info);

} // namespace BreezeCAD::UI::Status
