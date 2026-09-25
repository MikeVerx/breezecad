#pragma once
// ============================================================
// DocumentTree — дерево документа + контекстное меню.
//
// Содержит:
//   - DrawDocumentTree   — левая панель с деревом
//   - DrawLabelTree      — рекурсивный вывод метки (внутренняя)
//   - DrawContextMenuItems — пункты ПКМ-меню для метки
//
// Общий TreeContext — указатели на state Application + коллбеки.
// ============================================================

#include <functional>
#include <map>
#include <string>
#include <TDF_Label.hxx>

namespace BreezeCAD {
class BreezeDocument;
class OcctView;
class IconManager;
}

namespace BreezeCAD::UI::Dialogs {
struct AddBoxState;
struct RenameState;
struct DeleteConfirmState;
struct PropertiesState;
}

namespace BreezeCAD::UI::Panels {

struct TreeContext {
    // === Данные (read-only) ===
    BreezeDocument* doc   = nullptr;
    OcctView*       view  = nullptr;
    IconManager*    icons = nullptr;

    // === State (rw через указатели) ===
    std::map<std::string, bool>* visibility      = nullptr;
    std::map<std::string, bool>* isolationBackup = nullptr;

    Dialogs::AddBoxState*         addBoxState         = nullptr;
    Dialogs::RenameState*         renameState         = nullptr;
    Dialogs::DeleteConfirmState*  deleteConfirmState  = nullptr;
    Dialogs::PropertiesState*     propertiesState     = nullptr;

    bool* startExpandedManually = nullptr;
    bool* isolationActive       = nullptr;

    TDF_Label* pendingCreateSketchLabel = nullptr;
    TDF_Label* pendingEnterSketchLabel  = nullptr;
    TDF_Label* clipboardLabel           = nullptr;

    // ПКМ-меню в 3D-сцене (устанавливается в Run при клике)
    bool*   pendingContextMenu = nullptr;
    void*   contextMenuPos     = nullptr;  // ImVec2*

    // === Коллбеки ===
    std::function<void()> onSave;
    std::function<void()> onOpen;
    std::function<void(const TDF_Label&)> onEnterSketch;
    std::function<void()>                 onShowAllRespectingDefaults;
    std::function<void(const TDF_Label&)> onRequestDelete;
    std::function<void(const TDF_Label&)> onRenameRequest;
    std::function<void(const TDF_Label&)> onPropertiesRequest;
};

/// Левая панель с деревом документа.
void DrawDocumentTree(const TreeContext& ctx);

/// Рекурсивный вывод одной метки (вызывается из DrawDocumentTree).
bool DrawLabelTree(const TreeContext& ctx,
                   const TDF_Label& label,
                   const TDF_Label& selectedLabel,
                   bool hasOps);

/// Пункты контекстного меню (ПКМ) для метки.
void DrawContextMenuItems(const TreeContext& ctx,
                          const TDF_Label& label,
                          bool hasOps);

/// Плавающее окно контекстного меню в 3D-сцене.
void DrawContextMenu3D(const TreeContext& ctx);

} // namespace BreezeCAD::UI::Panels
