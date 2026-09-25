#include "PropertiesDialog.h"
#include "doc/BreezeDocument.h"
#include "util/Utf8.h"

#include "imgui.h"
#include <TDataStd_Name.hxx>

namespace BreezeCAD::UI::Dialogs {

void DrawPropertiesDialog(const PropertiesContext& ctx) {
    if (!ctx.state || !ctx.state->show) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Свойства", &ctx.state->show)) {
        if (ctx.state->pendingLabel.IsNull() || !ctx.doc) {
            ImGui::TextDisabled("Объект не выбран");
        } else {
            std::string name = "<unnamed>";
            Handle(TDataStd_Name) nameAttr;
            if (ctx.state->pendingLabel.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
                name = Util::ExtToUtf8(nameAttr->Get());
            }

            int type = BreezeDocument::GetType(ctx.state->pendingLabel);

            ImGui::Text("Имя: %s", name.c_str());
            ImGui::Text("Тип: %d", type);
            ImGui::Text("Тег: %d", ctx.state->pendingLabel.Tag());

            const char* typeName = "Неизвестно";
            switch (type) {
                case FT_Start:       typeName = "Папка Start"; break;
                case FT_Plane:       typeName = "Плоскость"; break;
                case FT_Axis:        typeName = "Ось"; break;
                case FT_OriginPoint: typeName = "Точка Origin"; break;
                case FT_Box:         typeName = "Куб"; break;
                case FT_Cylinder:    typeName = "Цилиндр"; break;
                case FT_Hole:        typeName = "Отверстие"; break;
                case FT_Sketch:      typeName = "Эскиз"; break;
                case FT_SketchLine:  typeName = "Линия эскиза"; break;
            }
            ImGui::Text("Описание: %s", typeName);

            ImGui::Separator();
            ImGui::TextDisabled("(расширенные свойства — позже)");
        }
    }
    ImGui::End();
}

} // namespace BreezeCAD::UI::Dialogs
