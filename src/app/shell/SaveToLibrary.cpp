// "В библиотеку": the open part saved into the user's library
// (doc::parts::AddUserItem). The part's variables chosen here become the
// columns of a table of sizes; the first row is the part as it is.

#include "app/Format.h"
#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "core/Expression.h"
#include "core/I18n.h"
#include "document/StandardPacks.h"
#include "document/StandardParts.h"
#include "ui/Widgets.h"

#include <imgui_stdlib.h>

#include <format>

namespace bz::app {

void OpenSaveToLibrary(AppContext& app) {
    const Session* s = app.Doc();
    if (!s || s->assembly) return;
    auto& st = app.shell.saveToLibrary;
    st = {};
    st.open = true;
    st.title = s->doc.info.name.empty() ? s->untitledName : s->doc.info.name;
    std::vector<std::string> row;
    for (const Parameter& v : s->doc.variables) {
        if (v.quantity != Quantity::Length) continue;   // sizes only
        st.variables.push_back(v.name);
        st.use.push_back(1);
        row.push_back(std::format("{}", v.value));
    }
    st.cells.push_back(std::move(row));
    if (st.variables.empty())
        app.notes.Error(Tr("У детали нет переменных-размеров: добавьте их на панели fx — они станут таблицей размеров"));
}

bool SaveLibraryItem(AppContext& app) {
    auto& st = app.shell.saveToLibrary;
    const Session* s = app.Doc();
    doc::parts::UserItem item;
    item.title = st.title;
    item.group = st.group;
    std::string problem;
    for (std::size_t i = 0; i < st.variables.size(); ++i)
        if (st.use[i]) item.variables.push_back(st.variables[i]);
    for (const auto& row : st.cells) {
        std::vector<double> values;
        for (std::size_t i = 0; i < st.variables.size(); ++i) {
            if (!st.use[i]) continue;
            const Expected<double> v = EvaluateExpression(row[i], Quantity::Length, UnitSystem{}, {});
            if (!v && problem.empty()) problem = Trf("«{}» в столбце {} — не число", row[i], st.variables[i]);
            values.push_back(v ? *v : 0.0);
        }
        item.rows.push_back(std::move(values));
    }
    if (!s || s->assembly) problem = Tr("Откройте деталь");
    if (!problem.empty()) {
        app.notes.Error(problem);
        return false;
    }
    const Expected<std::string> id = doc::parts::AddUserItem(StandardPackDir() / std::string(doc::parts::kUserPackFile), s->doc, item);
    if (!id) {
        app.notes.Error(Trf("Не сохранено: {}", id.GetError().message));
        return false;
    }
    LoadStandardPacks(app);
    app.notes.Success(Trf("«{}» — в моей библиотеке", item.title));
    if (const doc::parts::StandardInfo* added = doc::parts::FindStandard(*id)) {   // show it there
        auto& lib = app.shell.library;
        lib.chosen = {*id, doc::parts::Sizes(*added).front(), 0, {}};
        if (const auto range = doc::parts::FreeLength(*added)) lib.chosen.length = range->preset;
        lib.phoneGroup = -1;
        const auto groups = doc::parts::Groups();
        for (int g = 0; g < static_cast<int>(groups.size()); ++g)
            for (doc::parts::PartKind k : groups[static_cast<std::size_t>(g)].kinds)
                if (k == added->kind) lib.phoneGroup = g;
        app.shell.showLibrary = true;
    }
    st.open = false;
    return true;
}

void DrawSaveToLibrary(AppContext& app) {
    if (app.shell.mobile.phone) {   // full screen (mockup 6b)
        if (app.shell.saveToLibrary.open) DrawPhoneSaveToLibrary(app);
        return;
    }
    ui::Ui& ui = app.ui;
    auto& st = app.shell.saveToLibrary;
    if (!BeginDialog(ui, "##save-to-library", st.open, ui.S(760))) return;
    const auto close = [&] {
        st.open = false;
        ImGui::CloseCurrentPopup();
    };
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(Tr("Сохранить деталь в мою библиотеку"));
    }
    ui::FieldLabel(ui, Tr("Название изделия"));
    ui::TextField(ui, "##title", st.title, ImGui::GetContentRegionAvail().x);
    ui::FieldLabel(ui, Tr("Папка в окне «Библиотека»"));
    ui::TextField(ui, "##group", st.group, ImGui::GetContentRegionAvail().x);

    // Which variables are the table's columns; L is the length (any).
    ui::FieldLabel(ui, Tr("Переменные — столбцы таблицы размеров (L — длина, любая)"));
    for (std::size_t i = 0; i < st.variables.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, ui.S(14));
        bool on = st.use[i] != 0;
        if (ImGui::Checkbox(std::format("{}##use{}", st.variables[i], i).c_str(), &on)) st.use[i] = on ? 1 : 0;
    }

    // The table: a row per size, a cell per chosen variable.
    ui::FieldLabel(ui, Tr("Размеры (первая строка — деталь как есть)"));
    std::vector<std::size_t> shown;
    for (std::size_t i = 0; i < st.variables.size(); ++i)
        if (st.use[i] && st.variables[i] != "L") shown.push_back(i);
    const float cell = shown.empty() ? 0.0f : std::min(ui.S(110), (ImGui::GetContentRegionAvail().x - ui.S(40)) / static_cast<float>(shown.size()));
    ImGui::BeginChild("##rows", {0, ui.S(220)}, ImGuiChildFlags_Borders);
    const float x0 = ImGui::GetCursorPosX();
    for (std::size_t k = 0; k < shown.size(); ++k) {   // the column names over the cells
        if (k > 0) ImGui::SameLine(x0 + static_cast<float>(k) * (cell + ui.S(4)));
        ImGui::TextDisabled("%s", st.variables[shown[k]].c_str());
    }
    std::size_t removeRow = st.cells.size();
    for (std::size_t r = 0; r < st.cells.size(); ++r) {
        ImGui::PushID(static_cast<int>(r));
        for (std::size_t k = 0; k < shown.size(); ++k) {
            if (k > 0) ImGui::SameLine(0, ui.S(4));
            ImGui::SetNextItemWidth(cell);
            ImGui::InputTextWithHint(std::format("##c{}", shown[k]).c_str(), st.variables[shown[k]].c_str(), &st.cells[r][shown[k]]);
        }
        if (st.cells.size() > 1) {
            ImGui::SameLine(0, ui.S(6));
            if (ui::IconButton(ui, "##del", "ui-close", ui.S(26), ui.S(12), Tr("Убрать размер"))) removeRow = r;
        }
        ImGui::PopID();
    }
    if (removeRow < st.cells.size()) st.cells.erase(st.cells.begin() + static_cast<std::ptrdiff_t>(removeRow));
    if (ui::LinkButton(ui, "##add-row", Tr("+ Добавить размер"))) st.cells.push_back(st.cells.back());
    ImGui::EndChild();

    ImGui::Dummy({0, ui.S(6)});
    const float bw = ui.S(170);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2 * bw - ui.S(8));
    if (ui::SecondaryButton(ui, "##cancel", Tr("Отмена"), "Esc", {bw, ui.S(36)}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##save", Tr("Сохранить"), nullptr, {bw, ui.S(36)}) && SaveLibraryItem(app)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace bz::app
