#include "DocumentTree.h"
#include "doc/BreezeDocument.h"
#include "occt/OcctView.h"
#include "app/IconManager.h"
#include "app/Localization.h"
#include "util/LabelName.h"
#include "util/Utf8.h"
#include "ui/widgets/SvgButton.h"
#include "ui/dialogs/AddBoxDialog.h"
#include "ui/dialogs/RenameDialog.h"
#include "ui/dialogs/DeleteConfirmDialog.h"
#include "ui/dialogs/PropertiesDialog.h"

#include "imgui.h"
#include <TDF_ChildIterator.hxx>
#include <TDataStd_Name.hxx>

#include <cstring>
#include <iostream>

namespace BreezeCAD::UI::Panels {

void DrawContextMenuItems(const TreeContext& ctx,
                          const TDF_Label& label,
                          bool hasOps);

namespace {

bool DrawLabelTreeImpl(const TreeContext& ctx,
                       const TDF_Label& label,
                       const TDF_Label& selectedLabel,
                       bool hasOps) {
    if (label.IsNull()) return false;

    // Линии эскиза не показываем
    if (BreezeDocument::GetType(label) == FT_SketchLine) return false;

    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        labelName = Util::ExtToUtf8(nameAttr->Get());
    }

    int  type     = BreezeDocument::GetType(label);
    bool isSystem = (type < 0);

    TDF_ChildIterator childIt(label, false);
    bool hasChildren = childIt.More();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!selectedLabel.IsNull() && selectedLabel.IsEqual(label))
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(Util::LabelKey(label).c_str());

    // Чекбокс видимости для системных элементов
    if (isSystem && (type == FT_Plane || type == FT_Axis) && ctx.visibility && ctx.icons) {
        std::string key = Util::LabelKey(label);
        auto it = ctx.visibility->find(key);
        bool visible;
        if (it != ctx.visibility->end()) {
            visible = it->second;
        } else {
            visible = !hasOps;
        }

        const char* eyeName = visible ? "eye" : "eye-off";
        std::string eyeId = "eye_" + Util::LabelKey(label);
        if (UI::Widgets::SvgIconBtn(*ctx.icons, eyeId.c_str(), eyeName, 14, 22, 18,
                                     visible ? "Скрыть" : "Показать", true)) {
            (*ctx.visibility)[key] = !visible;
            if (ctx.view) ctx.view->SetShapeVisible(label, !visible);
        }
        ImGui::SameLine();
    }

    if (isSystem) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    }
    bool opened = ImGui::TreeNodeEx(labelName.c_str(), flags);
    if (isSystem) ImGui::PopStyleColor();

    // Двойной клик по эскизу → войти
    if (type == FT_Sketch && ImGui::IsItemHovered()
        && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
        && ctx.pendingEnterSketchLabel) {
        *ctx.pendingEnterSketchLabel = label;
    }

    // Контекстное меню
    if (ImGui::BeginPopupContextItem("##ctx")) {
        ImGui::TextDisabled("%s", labelName.c_str());
        ImGui::Separator();
        DrawContextMenuItems(ctx, label, hasOps);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("[type=%d, tag=%d]", type, label.Tag());

    bool requested = false;
    if (opened && hasChildren) {
        TDF_ChildIterator it(label, false);
        for (; it.More(); it.Next()) {
            if (DrawLabelTreeImpl(ctx, it.Value(), selectedLabel, hasOps)) requested = true;
        }
        ImGui::TreePop();
    }

    ImGui::PopID();

    bool pendingDelete = ctx.deleteConfirmState && !ctx.deleteConfirmState->pendingLabel.IsNull();
    bool pendingCreate = ctx.pendingCreateSketchLabel && !ctx.pendingCreateSketchLabel->IsNull();
    return requested || pendingDelete || pendingCreate;
}

} // anonymous namespace

bool DrawLabelTree(const TreeContext& ctx,
                   const TDF_Label& label,
                   const TDF_Label& selectedLabel,
                   bool hasOps) {
    return DrawLabelTreeImpl(ctx, label, selectedLabel, hasOps);
}

void DrawDocumentTree(const TreeContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    const float panelWidth = 340.0f;   // временно; если нужно — передавать через ctx

    ImGui::SetNextWindowPos(ImVec2(0, 211), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y - 211 - 76), ImGuiCond_Always);

    ImGui::Begin("##tree_panel", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("%s", Tr("panel.title").c_str());
    ImGui::Separator();

    if (ImGui::Button(Tr("panel.addBox").c_str()) && ctx.addBoxState) {
        ctx.addBoxState->show = true;
        std::strncpy(ctx.addBoxState->name, Tr("default.boxName").c_str(),
                     sizeof(ctx.addBoxState->name) - 1);
        ctx.addBoxState->name[sizeof(ctx.addBoxState->name) - 1] = '\0';
        ctx.addBoxState->pos[0] = ctx.addBoxState->pos[1] = ctx.addBoxState->pos[2] = 0.0f;
        ctx.addBoxState->size[0] = ctx.addBoxState->size[1] = ctx.addBoxState->size[2] = 100.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) { if (ctx.onSave) ctx.onSave(); }
    ImGui::SameLine();
    if (ImGui::Button("Open")) { if (ctx.onOpen) ctx.onOpen(); }
    ImGui::Separator();

    if (!ctx.doc || ctx.doc->GetDoc().IsNull()) {
        ImGui::Text("%s", Tr("panel.document.null").c_str());
        ImGui::End();
        return;
    }

    bool hasOps = ctx.doc->Has3DOperations();

    TDF_Label selectedLabel;
    if (ctx.view) selectedLabel = ctx.view->GetSelectedLabel();

    TDF_Label root = ctx.doc->MainLabel();

    if (ImGui::TreeNodeEx(Tr("panel.document").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {

        // === Папка Start ===
        TDF_Label baseLabel = ctx.doc->GetBaseGeometryLabel();
        if (!baseLabel.IsNull()) {
            bool startOpen = !hasOps || (ctx.startExpandedManually && *ctx.startExpandedManually);

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::SetNextItemOpen(startOpen, ImGuiCond_Always);
            bool opened = ImGui::TreeNodeEx("Start",
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::PopStyleColor();

            if (ImGui::IsItemToggledOpen() && ctx.startExpandedManually) {
                *ctx.startExpandedManually = opened;
            }

            if (opened) {
                TDF_ChildIterator it(baseLabel, false);
                for (; it.More(); it.Next()) {
                    TDF_Label child = it.Value();
                    if (BreezeDocument::GetType(child) == FT_OriginPoint) continue;
                    DrawLabelTreeImpl(ctx, child, selectedLabel, hasOps);
                }
                ImGui::TreePop();
            }
        }

        TDF_ChildIterator it(root, false);
        for (; it.More(); it.Next()) {
            TDF_Label child = it.Value();
            if (child.IsEqual(baseLabel)) continue;
            DrawLabelTreeImpl(ctx, child, selectedLabel, hasOps);
        }

        ImGui::TreePop();
    }

    ImGui::End();

    // === Обработка отложенных операций ===
    if (ctx.pendingCreateSketchLabel && !ctx.pendingCreateSketchLabel->IsNull() && ctx.doc) {
        TDF_Label newSketch = ctx.doc->AddSketchFeature(*ctx.pendingCreateSketchLabel);
        *ctx.pendingCreateSketchLabel = TDF_Label();
        if (!newSketch.IsNull() && ctx.pendingEnterSketchLabel) {
            *ctx.pendingEnterSketchLabel = newSketch;
            std::cout << "Auto-enter sketch: tag=" << newSketch.Tag() << "\n";
        }
    }
    if (ctx.pendingEnterSketchLabel && !ctx.pendingEnterSketchLabel->IsNull() && ctx.doc) {
        if (ctx.onEnterSketch) ctx.onEnterSketch(*ctx.pendingEnterSketchLabel);
        *ctx.pendingEnterSketchLabel = TDF_Label();
    }
}

void DrawContextMenuItems(const TreeContext& ctx,
                          const TDF_Label& label,
                          bool hasOps) {
    if (label.IsNull()) return;

    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        labelName = Util::ExtToUtf8(nameAttr->Get());
    }

    int  type     = BreezeDocument::GetType(label);
    bool isSystem = (type < 0);
    std::string key = Util::LabelKey(label);

    // Сброс изоляции
    if (ctx.isolationActive && *ctx.isolationActive) {
        if (ImGui::MenuItem("Показать всё (снять изоляцию)")) {
            if (ctx.onShowAllRespectingDefaults) ctx.onShowAllRespectingDefaults();
        }
        ImGui::Separator();
    }

    // Плоскость
    if (type == FT_Plane) {
        if (ImGui::MenuItem("Создать эскиз") && ctx.pendingCreateSketchLabel) {
            *ctx.pendingCreateSketchLabel = label;
        }
        ImGui::Separator();
    }

    // Эскиз
    if (type == FT_Sketch) {
        if (ImGui::MenuItem("Редактировать эскиз") && ctx.pendingEnterSketchLabel) {
            *ctx.pendingEnterSketchLabel = label;
        }
        ImGui::Separator();
    }

    // 3D-операция
    if (type == FT_Box || type == FT_Cylinder || type == FT_Hole) {
        if (ImGui::MenuItem("Редактировать")) {}
        if (ImGui::MenuItem("Измерить")) {}
        if (ImGui::MenuItem("Изолировать")) {
            if (ctx.view && ctx.doc && ctx.visibility && ctx.isolationBackup && ctx.isolationActive) {
                std::vector<TDF_Label> allLabels = ctx.view->GetAllLabels();
                *ctx.isolationBackup = *ctx.visibility;
                *ctx.isolationActive = true;

                for (const auto& entry : allLabels) {
                    if (entry.IsEqual(label)) continue;
                    if (BreezeDocument::IsSystem(entry)) continue;
                    std::string k = Util::LabelKey(entry);
                    (*ctx.visibility)[k] = false;
                    ctx.view->SetShapeVisible(entry, false);
                }
            }
        }
        ImGui::Separator();
    }

    // Start
    if (type == FT_Start) {
        if (ImGui::MenuItem("Показать все плоскости и оси")) {
            TDF_ChildIterator it(label, false);
            for (; it.More(); it.Next()) {
                TDF_Label child = it.Value();
                int ct = BreezeDocument::GetType(child);
                if (ct == FT_Plane || ct == FT_Axis) {
                    std::string k = Util::LabelKey(child);
                    if (ctx.visibility) (*ctx.visibility)[k] = true;
                    if (ctx.view) ctx.view->SetShapeVisible(child, true);
                }
            }
        }
        if (ImGui::MenuItem("Скрыть все плоскости и оси")) {
            TDF_ChildIterator it(label, false);
            for (; it.More(); it.Next()) {
                TDF_Label child = it.Value();
                int ct = BreezeDocument::GetType(child);
                if (ct == FT_Plane || ct == FT_Axis) {
                    std::string k = Util::LabelKey(child);
                    if (ctx.visibility) (*ctx.visibility)[k] = false;
                    if (ctx.view) ctx.view->SetShapeVisible(child, false);
                }
            }
        }
        ImGui::Separator();
    }

    // Скрыть / Показать
    if (isSystem && (type == FT_Plane || type == FT_Axis) && ctx.visibility) {
        auto it = ctx.visibility->find(key);
        bool curVisible = (it != ctx.visibility->end()) ? it->second : !hasOps;
        if (ImGui::MenuItem(curVisible ? "Скрыть" : "Показать")) {
            (*ctx.visibility)[key] = !curVisible;
            if (ctx.view) ctx.view->SetShapeVisible(label, !curVisible);
        }
        ImGui::Separator();
    }

    // Копировать
    if (!isSystem) {
        if (ImGui::MenuItem("Копировать") && ctx.clipboardLabel) {
            *ctx.clipboardLabel = label;
            std::cout << "Copied: " << labelName << "\n";
        }
    }
    if (ctx.clipboardLabel && !ctx.clipboardLabel->IsNull()) {
        if (ImGui::MenuItem("Вставить")) {
            std::cout << "Paste — TODO\n";
        }
    }
    if (!isSystem) ImGui::Separator();

    // Переименовать
    if (!isSystem) {
        if (ImGui::MenuItem("Переименовать")) {
            if (ctx.onRenameRequest) ctx.onRenameRequest(label);
        }
    }

    // Свойства
    if (ImGui::MenuItem("Свойства...")) {
        if (ctx.onPropertiesRequest) ctx.onPropertiesRequest(label);
    }

    // Удалить
    if (!isSystem) {
        ImGui::Separator();
        if (ImGui::MenuItem("Удалить")) {
            if (ctx.onRequestDelete) ctx.onRequestDelete(label);
        }
    }
}

} // namespace BreezeCAD::UI::Panels
