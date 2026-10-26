#include "app/features/DxfEditor.h"

#include "app/AppContext.h"
#include "app/mobile/MobileState.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "core/PathText.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace bz::app {
namespace {

using dxf::Drawing;
using dxf::Entity;
using dxf::Kind;

enum class Tool { Select, Text, Measure };

struct Editor {
    bool                  open = false;
    Drawing               drawing;
    std::string           title;
    std::filesystem::path path;
    bool                  dirty = false;
    std::string           skippedNote;   ///< what reading left out

    std::vector<Drawing> undo, redo;
    std::set<std::size_t> selected;

    // View: model point at the canvas centre and pixels per mm.
    Vec2   centre;
    double scale = 1.0;
    bool   fitPending = true;

    Tool tool = Tool::Select;
    // Selecting and dragging.
    bool   boxing = false, dragging = false, pressed = false;
    ImVec2 pressAt;
    Vec2   pressModel;
    std::optional<Drawing> beforeDrag;
    // Panels' fields.
    std::string moveX = "0", moveY = "0", angle = "90";
    std::string text = "Деталь 1", textHeight = "5";
    std::string textLayer = dxf::kMark;
    // Fingers on the drawing (the phone): where each was last frame, and
    // what a one-finger drag does — pans, or moves the selection it started on.
    std::map<int, ImVec2> fingersLast;
    bool fingerDecided = false, fingerMoves = false;
    bool sidePanel = false;   ///< the phone: the panels instead of the drawing
    // Measure: up to two picked points.
    std::vector<Vec2> measure;
    // The last check.
    std::optional<dxf::Check> check;
    std::string checkLayer;
    bool checkLayerChosen = false;
    std::string maxGap = "0.5";   ///< the user picked the layer; else «Рез» when used, or all
    bool confirmClose = false;
};

Editor& The() {
    static Editor e;
    return e;
}

/// AutoCAD colour index to RGB: drawing data, not interface colour — the
/// first nine as AutoCAD shows them, the rest by hue (index / 10 around the
/// wheel), close enough for a preview. 7 ("white", black on a light
/// background in AutoCAD) is the theme's text colour, so it reads on either theme.
ImU32 AciColor(int aci, ImU32 seven) {
    if (aci == 7 || aci == 0) return seven;
    static constexpr std::array<ImU32, 10> base{IM_COL32(255, 255, 255, 255), IM_COL32(255, 0, 0, 255),   IM_COL32(255, 255, 0, 255),
                                                IM_COL32(0, 255, 0, 255),     IM_COL32(0, 255, 255, 255), IM_COL32(0, 0, 255, 255),
                                                IM_COL32(255, 0, 255, 255),   IM_COL32(255, 255, 255, 255), IM_COL32(128, 128, 128, 255),
                                                IM_COL32(192, 192, 192, 255)};
    if (aci >= 0 && aci < static_cast<int>(base.size())) return base[static_cast<std::size_t>(aci)];
    if (aci >= 250) {
        const int g = 51 + (aci - 250) * 40;
        return IM_COL32(g, g, g, 255);
    }
    const float h = static_cast<float>((aci - 10) / 10 * 15 % 360) / 360.0f;
    const float v = (aci % 10) % 2 == 0 ? 1.0f : 0.65f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, 1.0f, v, r, g, b);
    return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), 255);
}

/// The colour as drawn on `background`: one too close to it in brightness
/// (yellow on the light theme, blue on the dark one) is pulled towards the
/// text colour until it stands out. Only the picture: the file keeps its colour.
ImU32 Readable(ImU32 colour, ImU32 background, ImU32 text) {
    const auto luminance = [](ImU32 c) {
        const auto ch = [](unsigned v) {
            const double x = v / 255.0;
            return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * ch(c & 0xFF) + 0.7152 * ch((c >> 8) & 0xFF) + 0.0722 * ch((c >> 16) & 0xFF);
    };
    const auto contrast = [&](ImU32 a, ImU32 b) {
        const double la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };
    // 3:1 — the WCAG level for lines and shapes (WCAG 2.1, 1.4.11).
    ImU32 out = colour;
    for (int step = 1; step <= 10 && contrast(out, background) < 3.0; ++step) {
        const float t = step / 10.0f;
        const auto mix = [&](int shift) {
            const float a = static_cast<float>((colour >> shift) & 0xFF), b = static_cast<float>((text >> shift) & 0xFF);
            return static_cast<ImU32>(a + (b - a) * t + 0.5f) << shift;
        };
        out = mix(0) | mix(8) | mix(16) | (colour & 0xFF000000u);
    }
    return out;
}

/// What a layer is for, in Russian, for the known shop layers.
std::string LayerLabel(const std::string& name) {
    if (name == dxf::kCut) return std::string(Tr("Рез")) + " (CUT)";
    if (name == dxf::kBend) return std::string(Tr("Гиб")) + " (BEND)";
    if (name == dxf::kMark) return std::string(Tr("Маркировка")) + " (MARK)";
    return name;
}

double Parse(const std::string& s, double fallback) {
    std::string t = s;
    std::replace(t.begin(), t.end(), ',', '.');
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return end && end != t.c_str() ? v : fallback;
}

void Fit(Editor& e, ImVec2 size) {
    Box2 box = dxf::Bounds(e.drawing, true);
    if (box.IsEmpty()) box.Include(Vec2{}), box.Include(Vec2{100, 100});
    const Vec2 span = box.max - box.min;
    e.centre = (box.min + box.max) * 0.5;
    e.scale = std::min((size.x * 0.9) / std::max(span.x, 1e-6), (size.y * 0.9) / std::max(span.y, 1e-6));
    if (!std::isfinite(e.scale) || e.scale <= 0) e.scale = 1.0;
    e.fitPending = false;
}

void Remember(Editor& e) {
    e.undo.push_back(e.drawing);
    if (e.undo.size() > 100) e.undo.erase(e.undo.begin());
    e.redo.clear();
    e.dirty = true;
    e.check.reset();
}

bool LayerVisible(const Editor& e, const Entity& en) {
    const dxf::Layer* l = e.drawing.FindLayer(en.layer);
    return !l || l->visible;
}

ImU32 ColorOf(const Editor& e, const Entity& en, ImU32 seven, ImU32 background) {
    if (en.color != 256) return Readable(AciColor(en.color, seven), background, seven);
    const dxf::Layer* l = e.drawing.FindLayer(en.layer);
    return Readable(AciColor(l ? l->color : 7, seven), background, seven);
}

/// The layer's colour: a square to click and the AutoCAD colours to choose from.
/// Returns true when one was picked (`aci` changed).
bool ColourPicker(ui::Ui& ui, const char* id, ImVec2 at, float h, int& aci) {
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float side = ui.S(12);
    ImGui::SetCursorScreenPos({at.x, at.y});
    ImGui::InvisibleButton(id, {side + ui.S(8), h});
    const bool clicked = ImGui::IsItemClicked();
    if (ImGui::IsItemHovered()) ui::ItemTooltip(ui, Tr("Цвет слоя"));
    const ImVec2 a{at.x + ui.S(4), at.y + h / 2 - side / 2}, b{a.x + side, a.y + side};
    dl->AddRectFilled(a, b, AciColor(aci, ui::ToU32(c.text)), ui.S(3));
    dl->AddRect(a, b, ui::ToU32(c.fieldBorder), ui.S(3));
    if (clicked) ImGui::OpenPopup(id);
    bool picked = false;
    if (ImGui::BeginPopup(id)) {
        // The standard nine, the greys and a row of hues (AutoCAD index colours).
        static constexpr std::array<int, 24> choices{1,  2,  3,  4,  5,  6,  7,  8,  9,  30, 40, 50,
                                                     70, 90, 110, 130, 150, 170, 190, 210, 230, 250, 252, 254};
        const float cell = ui.S(28);
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (i % 8 != 0) ImGui::SameLine(0, ui.S(4));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::InvisibleButton("c", {cell, cell})) {
                aci = choices[i];
                picked = true;
                ImGui::CloseCurrentPopup();
            }
            const bool hot = ImGui::IsItemHovered();
            ImDrawList* pl = ImGui::GetWindowDrawList();
            pl->AddRectFilled(p, {p.x + cell, p.y + cell}, AciColor(choices[i], ui::ToU32(c.text)), ui.S(4));
            pl->AddRect(p, {p.x + cell, p.y + cell}, ui::ToU32(hot || aci == choices[i] ? c.accent : c.fieldBorder), ui.S(4), 0,
                        ui.S(hot || aci == choices[i] ? 2.0f : 1.0f));
            if (hot) ui::ItemTooltip(ui, std::format("{} {}", Tr("Цвет AutoCAD"), choices[i]).c_str());
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    return picked;
}

void DeleteSelected(Editor& e) {
    if (e.selected.empty()) return;
    Remember(e);
    std::vector<Entity> kept;
    for (std::size_t i = 0; i < e.drawing.entities.size(); ++i)
        if (!e.selected.count(i)) kept.push_back(std::move(e.drawing.entities[i]));
    e.drawing.entities = std::move(kept);
    e.selected.clear();
}

void Undo(Editor& e) {
    if (e.undo.empty()) return;
    e.redo.push_back(std::move(e.drawing));
    e.drawing = std::move(e.undo.back());
    e.undo.pop_back();
    e.selected.clear();
    e.check.reset();
    e.dirty = true;
}

void Redo(Editor& e) {
    if (e.redo.empty()) return;
    e.undo.push_back(std::move(e.drawing));
    e.drawing = std::move(e.redo.back());
    e.redo.pop_back();
    e.selected.clear();
    e.check.reset();
    e.dirty = true;
}

/// Middle of the selection's box: where rotating and mirroring turn around.
Vec2 SelectionMiddle(const Editor& e) {
    Box2 box;
    for (const std::size_t i : e.selected) {
        const Box2 b = dxf::Bounds(e.drawing.entities[i]);
        if (!b.IsEmpty()) box.Include(b.min), box.Include(b.max);
    }
    return box.IsEmpty() ? Vec2{} : (box.min + box.max) * 0.5;
}

/// Draws `text` rotated by `deg` around its first corner (ImGui has no rotated text: the vertices are turned).
void RotatedText(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 col, const std::string& text, double deg) {
    const int first = dl->VtxBuffer.Size;
    dl->AddText(font, size, {at.x, at.y - size}, col, text.c_str());   // its baseline at `at`
    if (std::abs(deg) < 1e-9) return;
    const float a = static_cast<float>(-deg * kPi / 180.0), c = std::cos(a), s = std::sin(a);
    for (int i = first; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& p = dl->VtxBuffer[i].pos;
        const float dx = p.x - at.x, dy = p.y - at.y;
        p = {at.x + dx * c - dy * s, at.y + dx * s + dy * c};
    }
}

// --- the window parts ---------------------------------------------------------

void Section(ui::Ui& ui, const char* text) {
    ImGui::Dummy({0, ui.S(4)});
    ui::FontScope font(ui.fonts.strong);
    ImGui::TextUnformatted(text);
}

void Caption(ui::Ui& ui, const std::string& text) {
    ui::FontScope font(ui.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

bool SaveTo(Editor& e, const std::filesystem::path& file, std::string* error) {
    std::ofstream out(file, std::ios::binary);
    out << dxf::WriteR12(e.drawing);
    out.close();
    if (!out || !platform::FileWritten(file)) {
        if (error) *error = std::string(Tr("Не удалось записать файл"));
        return false;
    }
    e.path = file;
    e.title = ToUtf8(file.filename());
    e.dirty = false;
    return true;
}

bool SaveAs(AppContext& app, Editor& e) {
    if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
        app.notes.Error(problem);
        return false;
    }
    const std::filesystem::path folder = e.path.empty() ? platform::DocumentsDir() : e.path.parent_path();
    std::string name = e.path.empty() ? e.title : ToUtf8(e.path.filename());
    if (!name.ends_with(".dxf")) name += ".dxf";
    const auto file = platform::ShowSaveDialog(Tr("Сохранить DXF"), folder / PathFromUtf8(name), {{"DXF", "*.dxf"}}, ".dxf");
    if (!file) return false;
    std::string error;
    if (!SaveTo(e, *file, &error)) {
        app.notes.Error(error);
        return false;
    }
    app.notes.Info(Trf("Сохранено: {}", e.title));
    return true;
}

void Save(AppContext& app, Editor& e) {
    if (e.path.empty()) {
        SaveAs(app, e);
        return;
    }
    std::string error;
    if (SaveTo(e, e.path, &error)) app.notes.Info(Trf("Сохранено: {}", e.title));
    else app.notes.Error(error);
}

void OpenAsk(AppContext& app, Editor& e) {
    if (const std::string problem = platform::FileDialogProblem(); !problem.empty()) {
        app.notes.Error(problem);
        return;
    }
    const auto file = platform::ShowOpenDialog(Tr("Открыть DXF"), {{"DXF", "*.dxf"}},
                                               e.path.empty() ? platform::DocumentsDir() : e.path.parent_path());
    if (!file) return;
    std::string error;
    if (!OpenDxfEditorFile(app, *file, &error)) app.notes.Error(error);
}

/// Narrow screens (the phone): a row of icons, the drawing or the panels in turn.
void DrawCompactToolbar(AppContext& app, Editor& e, float width) {
    ui::Ui& ui = app.ui;
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted((e.title + (e.dirty ? " *" : "")).c_str());
    }
    const float b = ui.S(40), gap = ui.S(4), icon = ui.S(20);
    struct Button {
        const char* id;
        const char* icon;
        const char* tip;
        bool enabled, active;
    };
    const std::array<Button, 11> row{{
        {"##c-panels", "ui-sliders", Tr("Панели: слои, проверка, перенос"), true, e.sidePanel},
        {"##c-select", "ui-cursor", Tr("Выбор"), true, !e.sidePanel && e.tool == Tool::Select},
        {"##c-text", "ui-edit", Tr("Текст"), true, !e.sidePanel && e.tool == Tool::Text},
        {"##c-measure", "ui-ruler", Tr("Замер"), true, !e.sidePanel && e.tool == Tool::Measure},
        {"##c-delete", "ui-trash", Tr("Удалить выбранное"), !e.selected.empty(), false},
        {"##c-undo", "ui-undo", Tr("Отменить"), !e.undo.empty(), false},
        {"##c-redo", "ui-redo", Tr("Повторить"), !e.redo.empty(), false},
        {"##c-fit", "ui-maximize", Tr("Вписать"), true, false},
        {"##c-open", "ui-open", Tr("Открыть…"), true, false},
        {"##c-save", "ui-save", Tr("Сохранить"), true, false},
        {"##c-close", "ui-close", Tr("Закрыть"), true, false},
    }};
    float x = 0;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i > 0) {
            if (x + gap + b <= width) ImGui::SameLine(0, gap), x += gap;
            else x = 0;   // the next row
        }
        x += b;
        if (!ui::IconButton(ui, row[i].id, row[i].icon, b, icon, row[i].tip, row[i].enabled, row[i].active) || !row[i].enabled) continue;
        switch (i) {
            case 0: e.sidePanel = !e.sidePanel; break;
            case 1: case 2: case 3:
                e.tool = static_cast<Tool>(i - 1);
                e.measure.clear();
                e.sidePanel = false;
                break;
            case 4: DeleteSelected(e); break;
            case 5: Undo(e); break;
            case 6: Redo(e); break;
            case 7: e.fitPending = true; e.sidePanel = false; break;
            case 8: OpenAsk(app, e); break;
            case 9: Save(app, e); break;
            default:
                if (e.dirty) e.confirmClose = true;
                else e.open = false;
        }
    }
}

void DrawToolbar(AppContext& app, Editor& e, float width) {
    ui::Ui& ui = app.ui;
    const ImVec2 bs{ui.S(118), ui.S(32)};
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted((std::string(Tr("Редактор DXF")) + " — " + e.title + (e.dirty ? " *" : "")).c_str());
    }
    ImGui::SameLine(width - (bs.x * 7 + ui.S(8) * 6));
    if (ui::SecondaryButton(ui, "##dxf-open", Tr("Открыть…"), nullptr, bs)) OpenAsk(app, e);
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##dxf-save", Tr("Сохранить"), "Ctrl+S", bs)) Save(app, e);
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##dxf-saveas", Tr("Сохранить как…"), nullptr, bs)) SaveAs(app, e);
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##dxf-undo", Tr("Отменить"), "Ctrl+Z", bs, !e.undo.empty())) Undo(e);
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##dxf-redo", Tr("Повторить"), "Ctrl+Y", bs, !e.redo.empty())) Redo(e);
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##dxf-fit", Tr("Вписать"), "F", bs)) e.fitPending = true;
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##dxf-close", Tr("Закрыть"), nullptr, bs)) {
        if (e.dirty) e.confirmClose = true;
        else e.open = false;
    }
}

void DrawLayers(AppContext& app, Editor& e, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Section(ui, Tr("Слои"));
    std::map<std::string, int> counts;
    for (const Entity& en : e.drawing.entities) ++counts[en.layer];
    for (std::size_t i = 0; i < e.drawing.layers.size(); ++i) {
        dxf::Layer& l = e.drawing.layers[i];
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(28);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsMouseHoveringRect(p, {p.x + width, p.y + h});
        if (hovered) dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.hoverRow), ui.S(6));
        // Eye: visible or not.
        if (ImGui::InvisibleButton("eye", {ui.S(26), h})) l.visible = !l.visible;
        ui.icons.Draw(dl, l.visible ? "ui-eye" : "ui-eye-off", {p.x + ui.S(5), p.y + (h - ui.S(16)) / 2}, ui.S(16),
                      l.visible ? c.textSecondary : c.textDisabled);
        int colour = l.color;
        if (ColourPicker(ui, "colour", {p.x + ui.S(26), p.y}, h, colour) && colour != l.color) {
            Remember(e);
            e.drawing.layers[i].color = colour;
        }
        const std::string label = LayerLabel(l.name);
        dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(50), p.y + (h - ui.S(13)) / 2}, ui::ToU32(l.visible ? c.text : c.textDisabled),
                    label.c_str());
        const std::string n = std::to_string(counts[l.name]);
        const ImVec2 ns = ui.fonts.mono->CalcTextSizeA(ui.S(12), FLT_MAX, 0, n.c_str());
        // «→» puts the selection on this layer.
        const float arrowW = ui.S(26);
        dl->AddText(ui.fonts.mono, ui.S(12), {p.x + width - arrowW - ns.x - ui.S(8), p.y + (h - ui.S(12)) / 2}, ui::ToU32(c.textMuted), n.c_str());
        ImGui::SetCursorScreenPos({p.x + ui.S(46), p.y});
        ImGui::InvisibleButton("row", {width - ui.S(46) - arrowW, h});
        if (ImGui::IsItemClicked()) {   // select everything on the layer
            e.selected.clear();
            for (std::size_t k = 0; k < e.drawing.entities.size(); ++k)
                if (e.drawing.entities[k].layer == l.name) e.selected.insert(k);
        }
        if (ImGui::IsItemHovered()) ui::ItemTooltip(ui, Tr("Щелчок — выбрать всё на этом слое"));
        ImGui::SameLine(0, 0);
        const bool canMove = !e.selected.empty();
        if (ui::IconButton(ui, "to", "ui-chevron-right", arrowW, ui.S(14), Tr("Перенести выбранное на этот слой"), canMove) && canMove) {
            Remember(e);
            for (const std::size_t k : e.selected) e.drawing.entities[k].layer = l.name, e.drawing.entities[k].color = 256;
        }
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ImGui::PopID();
    }
    Caption(ui, Tr("Глаз — показать или скрыть слой, квадрат — цвет слоя. Стрелка — перенести выбранное на слой. Станок режет по слою «Рез»."));
}

void DrawSelection(AppContext& app, Editor& e, float width) {
    ui::Ui& ui = app.ui;
    Section(ui, Trf("Выбрано: {}", e.selected.size()).c_str());
    if (e.selected.empty()) {
        if (app.shell.mobile.touchScreen)
            Caption(ui, Tr("Касание — выбрать или снять, по одному. Палец по выбранному — перенести, по пустому месту — сдвинуть вид, двумя пальцами — масштаб."));
        else
            Caption(ui, Tr("Щелчок — выбрать, Shift — добавить. Рамка слева направо — что целиком внутри, справа налево — что задето."));
        return;
    }
    const float half = (width - ui.S(8)) / 2;
    ui::FieldLabel(ui, Tr("Сдвиг X / Y, мм"));
    ui::TextField(ui, "##dx", e.moveX, half);
    ImGui::SameLine(0, ui.S(8));
    ui::TextField(ui, "##dy", e.moveY, half);
    if (ui::SecondaryButton(ui, "##move", Tr("Сдвинуть"), nullptr, {width, ui.S(30)})) {
        Remember(e);
        const Vec2 by{Parse(e.moveX, 0), Parse(e.moveY, 0)};
        for (const std::size_t i : e.selected) dxf::Move(e.drawing.entities[i], by);
    }
    ui::FieldLabel(ui, Tr("Поворот, °"));
    ui::TextField(ui, "##angle", e.angle, half);
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##rotate", Tr("Повернуть"), nullptr, {half, ui.S(30)})) {
        Remember(e);
        const Vec2 m = SelectionMiddle(e);
        const double a = Parse(e.angle, 0);
        for (const std::size_t i : e.selected) dxf::Rotate(e.drawing.entities[i], m, a);
    }
    if (ui::SecondaryButton(ui, "##mirror", Tr("Зеркально"), nullptr, {half, ui.S(30)})) {
        Remember(e);
        const double x = SelectionMiddle(e).x;
        for (const std::size_t i : e.selected) dxf::MirrorX(e.drawing.entities[i], x);
    }
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##delete", Tr("Удалить"), "Del", {half, ui.S(30)})) DeleteSelected(e);
}

void DrawToolPanel(AppContext& app, Editor& e, float width) {
    ui::Ui& ui = app.ui;
    Section(ui, Tr("Инструмент"));
    std::array<const char*, 3> tools{Tr("Выбор"), Tr("Текст"), Tr("Замер")};
    const int current = static_cast<int>(e.tool);
    if (const int t = ui::Segmented(ui, "dxf-tool", tools, current, 3, width); t != current && t >= 0) {
        e.tool = static_cast<Tool>(t);
        e.measure.clear();
    }
    if (e.tool == Tool::Text) {
        ui::FieldLabel(ui, Tr("Текст маркировки"));
        ui::TextField(ui, "##text", e.text, width);
        ui::FieldLabel(ui, Tr("Высота букв, мм"));
        ui::TextField(ui, "##th", e.textHeight, width);
        Caption(ui, Trf("Щёлкните или коснитесь чертежа там, где начать строку. Ляжет на слой «{}».", LayerLabel(e.textLayer)));
    } else if (e.tool == Tool::Measure) {
        if (e.measure.size() == 2) {
            const Vec2 d = e.measure[1] - e.measure[0];
            ImGui::TextUnformatted(Trf("Расстояние {:.3f} мм", d.Length()).c_str());
            ImGui::TextUnformatted(Trf("ΔX {:.3f}   ΔY {:.3f}", d.x, d.y).c_str());
        }
        Caption(ui, Tr("Две точки: концы, середины и центры притягивают курсор."));
    }
}

void DrawCheckPanel(AppContext& app, Editor& e, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Section(ui, Tr("Проверка для резки"));
    // Which layer to look at: «Рез» when something is on it, else all.
    if (!e.checkLayerChosen) {
        e.checkLayer.clear();
        for (const Entity& en : e.drawing.entities)
            if (en.layer == dxf::kCut) e.checkLayer = dxf::kCut;
    }
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##check-layer", e.checkLayer.empty() ? Tr("Все слои") : LayerLabel(e.checkLayer).c_str())) {
        if (ImGui::Selectable(Tr("Все слои"), e.checkLayer.empty())) e.checkLayer.clear(), e.checkLayerChosen = true, e.check.reset();
        for (const dxf::Layer& l : e.drawing.layers)
            if (ImGui::Selectable(LayerLabel(l.name).c_str(), e.checkLayer == l.name)) e.checkLayer = l.name, e.checkLayerChosen = true, e.check.reset();
        ImGui::EndCombo();
    }
    if (ui::SecondaryButton(ui, "##check", Tr("Проверить контуры"), nullptr, {width, ui.S(30)})) e.check = dxf::CheckForCutting(e.drawing, e.checkLayer);
    if (!e.check) {
        Caption(ui, Tr("Разрывы контура (станок не замкнёт рез), наложенные дубли (прожжёт дважды), число замкнутых контуров."));
        return;
    }
    const dxf::Check& k = *e.check;
    const auto row = [&](const std::string& text, bool bad) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(bad ? c.danger : c.success));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    };
    row(Trf("Разрывов: {}", k.openEnds.size()), !k.openEnds.empty());
    row(Trf("Дублей: {}", k.duplicates.size()), !k.duplicates.empty());
    ImGui::TextUnformatted(Trf("Замкнутых контуров: {}", k.closedContours).c_str());
    if (!k.duplicates.empty() && ui::SecondaryButton(ui, "##dups", Tr("Удалить дубли"), nullptr, {width, ui.S(30)})) {
        e.selected = {k.duplicates.begin(), k.duplicates.end()};
        DeleteSelected(e);
        e.check = dxf::CheckForCutting(e.drawing, e.checkLayer);
    }
    if (!k.openEnds.empty()) {
        Caption(ui, Tr("Разрывы отмечены красными кругами на чертеже."));
        const float half = (width - ui.S(8)) / 2;
        ui::TextField(ui, "##gap", e.maxGap, half);
        ImGui::SameLine(0, ui.S(8));
        if (ui::SecondaryButton(ui, "##gaps", Tr("Сшить разрывы"), nullptr, {half, ui.S(30)})) {
            Remember(e);
            const int n = dxf::CloseGaps(e.drawing, e.checkLayer, Parse(e.maxGap, 0.5));
            e.check = dxf::CheckForCutting(e.drawing, e.checkLayer);
            app.notes.Info(Trf("Сшито разрывов: {}", n));
        }
        Caption(ui, Tr("Сшивает концы, между которыми не больше указанного, мм. Дальние разрывы — вручную: они могут быть ошибкой чертежа."));
    }
}

/// The nearest visible entity within `px` screen pixels of `at`.
std::optional<std::size_t> PickAt(const Editor& e, Vec2 at, double px) {
    std::optional<std::size_t> under;
    double best = px / e.scale;
    for (std::size_t i = 0; i < e.drawing.entities.size(); ++i) {
        const Entity& en = e.drawing.entities[i];
        if (!LayerVisible(e, en)) continue;
        const double d = dxf::Distance(en, at);
        if (d < best) best = d, under = i;
    }
    return under;
}

/// Fingers on the drawing (mobile/TouchInput hands them over raw): one finger
/// pans, or drags the selection it started on; two pinch to zoom and pan; a
/// tap does what a click does — picks (a tap on a picked one drops it, so
/// several are picked tap by tap), places the text, sets a measure point.
template <class ToModel>
void Fingers(AppContext& app, Editor& e, ImVec2 p0, ImVec2 size, const ToModel& toModel) {
    ui::Ui& ui = app.ui;
    mobile::MobileState& m = app.shell.mobile;
    // Only when nothing is open over the drawing (a menu, a colour list).
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        m.canvasSeen = true;
        m.canvasMin = p0;
        m.canvasMax = {p0.x + size.x, p0.y + size.y};
    }
    const auto& fingers = m.canvasFingers;
    std::map<int, ImVec2> now;
    for (const auto& f : fingers) now[f.id] = f.at;
    if (fingers.size() == 1 && e.fingersLast.count(fingers[0].id)) {
        const auto& f = fingers[0];
        const ImVec2 last = e.fingersLast[f.id];
        if (!e.fingerDecided && std::hypot(f.at.x - f.start.x, f.at.y - f.start.y) > ui.S(10)) {
            e.fingerDecided = true;
            const Vec2 start = toModel(f.start);
            e.fingerMoves = false;
            if (e.tool == Tool::Select)
                for (const std::size_t i : e.selected)
                    if (dxf::Distance(e.drawing.entities[i], start) < ui.S(14) / e.scale) e.fingerMoves = true;
            if (e.fingerMoves) e.beforeDrag = e.drawing;
        }
        if (e.fingerDecided) {
            const Vec2 by = toModel(f.at) - toModel(last);
            if (e.fingerMoves) {
                for (const std::size_t i : e.selected) dxf::Move(e.drawing.entities[i], by);
            } else {
                e.centre = e.centre - by;
            }
        }
    } else if (fingers.size() >= 2 && e.fingersLast.count(fingers[0].id) && e.fingersLast.count(fingers[1].id)) {
        // Pinch: the point between the fingers stays under them.
        const ImVec2 a0 = e.fingersLast[fingers[0].id], b0 = e.fingersLast[fingers[1].id];
        const ImVec2 a1 = fingers[0].at, b1 = fingers[1].at;
        const double d0 = std::hypot(a0.x - b0.x, a0.y - b0.y), d1 = std::hypot(a1.x - b1.x, a1.y - b1.y);
        const ImVec2 m0{(a0.x + b0.x) / 2, (a0.y + b0.y) / 2}, m1{(a1.x + b1.x) / 2, (a1.y + b1.y) / 2};
        const Vec2 under = toModel(m0);
        if (d0 > 1.0 && d1 > 1.0) e.scale = std::clamp(e.scale * d1 / d0, 1e-4, 1e5);
        // After the zoom, put `under` back below the new midpoint.
        const Vec2 moved = toModel(m1);
        e.centre = e.centre + (under - moved);
        e.fingerDecided = true;
        e.fingerMoves = false;
    }
    if (fingers.empty() && e.fingerMoves && e.beforeDrag) {
        e.undo.push_back(std::move(*e.beforeDrag));
        e.redo.clear();
        e.dirty = true;
        e.check.reset();
    }
    if (fingers.empty()) {
        e.fingerDecided = e.fingerMoves = false;
        if (!e.pressed) e.beforeDrag.reset();
    }
    e.fingersLast = std::move(now);

    if (!m.canvasTapped) return;
    m.canvasTapped = false;
    const Vec2 at = toModel(m.canvasTapAt);
    if (e.tool == Tool::Select) {
        if (const auto under = PickAt(e, at, ui.S(14))) {
            if (!e.selected.erase(*under)) e.selected.insert(*under);
        } else {
            e.selected.clear();
        }
    } else if (e.tool == Tool::Text) {
        Remember(e);
        e.drawing.EnsureLayer(e.textLayer, 3);
        e.drawing.entities.push_back(Entity::Text(at, std::max(0.5, Parse(e.textHeight, 5)), 0, e.text, e.textLayer));
        e.selected = {e.drawing.entities.size() - 1};
    } else if (e.tool == Tool::Measure) {
        std::optional<Vec2> snap;
        double best = ui.S(18) / e.scale;
        for (const Entity& en : e.drawing.entities) {
            if (!LayerVisible(e, en)) continue;
            for (const Vec2 v : dxf::SnapPoints(en))
                if (const double d = (v - at).Length(); d < best) best = d, snap = v;
        }
        if (e.measure.size() >= 2) e.measure.clear();
        e.measure.push_back(snap ? *snap : at);
    }
}

/// The drawing area: drawing, picking, dragging, the tools.
void DrawCanvas(AppContext& app, Editor& e, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##dxf-canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, {p0.x + size.x, p0.y + size.y}, true);
    dl->AddRectFilled(p0, {p0.x + size.x, p0.y + size.y}, ui::ToU32(c.field), ui.S(8));
    dl->AddRect(p0, {p0.x + size.x, p0.y + size.y}, ui::ToU32(c.fieldBorder), ui.S(8));
    if (e.fitPending) Fit(e, size);

    const ImVec2 mid{p0.x + size.x / 2, p0.y + size.y / 2};
    const auto toScreen = [&](Vec2 v) {
        return ImVec2{mid.x + static_cast<float>((v.x - e.centre.x) * e.scale), mid.y - static_cast<float>((v.y - e.centre.y) * e.scale)};
    };
    const auto toModel = [&](ImVec2 s) { return Vec2{e.centre.x + (s.x - mid.x) / e.scale, e.centre.y - (s.y - mid.y) / e.scale}; };
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const Vec2 cursor = toModel(mouse);

    // Zoom at the cursor, pan with the right or middle button.
    if (hovered && io.MouseWheel != 0) {
        const double k = std::pow(1.2, io.MouseWheel);
        e.centre = cursor + (e.centre - cursor) * (1.0 / k);
        e.scale *= k;
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        e.centre = e.centre - Vec2{io.MouseDelta.x / e.scale, -io.MouseDelta.y / e.scale};
    }
    Fingers(app, e, p0, size, toModel);

    // A light grid every 10 mm when zoomed in enough, every 100 otherwise.
    {
        const double stepMm = e.scale * 10 > ui.S(14) ? 10.0 : 100.0;
        const Vec2 lo = toModel(p0), hi = toModel({p0.x + size.x, p0.y + size.y});
        if ((hi.x - lo.x) / stepMm < 400)
            for (double x = std::floor(lo.x / stepMm) * stepMm; x <= hi.x; x += stepMm) {
                const float sx = toScreen({x, 0}).x;
                dl->AddLine({sx, p0.y}, {sx, p0.y + size.y}, ui::ToU32(std::abs(x) < 1e-9 ? c.sketchGridMajor : c.sketchGrid));
            }
        if ((lo.y - hi.y) / stepMm < 400)
            for (double y = std::floor(hi.y / stepMm) * stepMm; y <= lo.y; y += stepMm) {
                const float sy = toScreen({0, y}).y;
                dl->AddLine({p0.x, sy}, {p0.x + size.x, sy}, ui::ToU32(std::abs(y) < 1e-9 ? c.sketchGridMajor : c.sketchGrid));
            }
    }

    // What is under the cursor (nearest within a few pixels).
    std::optional<std::size_t> under;
    if (hovered && !e.boxing && app.shell.mobile.canvasFingers.empty()) under = PickAt(e, cursor, ui.S(6));

    // The entities.
    const ImU32 selectedColor = ui::ToU32(c.sketchSelected), hoverColor = ui::ToU32(c.sketchHover), seven = ui::ToU32(c.text);
    for (std::size_t i = 0; i < e.drawing.entities.size(); ++i) {
        const Entity& en = e.drawing.entities[i];
        if (!LayerVisible(e, en)) continue;
        const bool sel = e.selected.count(i) > 0;
        const bool hot = under && *under == i;
        const ImU32 col = sel ? selectedColor : hot ? hoverColor : ColorOf(e, en, seven, ui::ToU32(c.field));
        const float thick = ui.S(sel || hot ? 2.2f : 1.2f);
        if (en.kind == Kind::Text) {
            const float px = static_cast<float>(en.height * e.scale);
            if (px >= 3.0f) RotatedText(dl, ui.fonts.body, px, toScreen(en.at), col, en.text, en.rotationDeg);
            continue;
        }
        const double maxAngle = std::clamp(180.0 / std::max(1.0, en.radius * e.scale), 1.0, 10.0);
        const std::vector<Vec2> pts = dxf::Outline(en, maxAngle);
        std::vector<ImVec2> sp;
        sp.reserve(pts.size());
        for (const Vec2 v : pts) sp.push_back(toScreen(v));
        if (sp.size() > 1) dl->AddPolyline(sp.data(), static_cast<int>(sp.size()), col, ImDrawFlags_None, thick);
    }

    // Open ends found by the check.
    if (e.check)
        for (const Vec2 v : e.check->openEnds) dl->AddCircle(toScreen(v), ui.S(9), ui::ToU32(c.danger), 0, ui.S(2));

    // Snapping for the measure and text tools.
    std::optional<Vec2> snap;
    if (hovered && e.tool == Tool::Measure) {
        double best = ui.S(10);
        for (const Entity& en : e.drawing.entities) {
            if (!LayerVisible(e, en)) continue;
            for (const Vec2 v : dxf::SnapPoints(en)) {
                const ImVec2 s = toScreen(v);
                const double d = std::hypot(s.x - mouse.x, s.y - mouse.y);
                if (d < best) best = d, snap = v;
            }
        }
        if (snap) dl->AddRect({toScreen(*snap).x - ui.S(5), toScreen(*snap).y - ui.S(5)}, {toScreen(*snap).x + ui.S(5), toScreen(*snap).y + ui.S(5)},
                              ui::ToU32(c.success), 0, 0, ui.S(1.5f));
    }

    // Clicks and drags of the left button.
    const bool typing = io.WantTextInput;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        e.pressed = true;
        e.pressAt = mouse;
        e.pressModel = cursor;
        e.boxing = e.dragging = false;
        if (e.tool == Tool::Select) {
            if (under) {
                if (io.KeyShift) {
                    if (!e.selected.erase(*under)) e.selected.insert(*under);
                } else if (!e.selected.count(*under)) {
                    e.selected = {*under};
                }
            }
        } else if (e.tool == Tool::Text) {
            Remember(e);
            e.drawing.EnsureLayer(e.textLayer, 3);
            e.drawing.entities.push_back(Entity::Text(cursor, std::max(0.5, Parse(e.textHeight, 5)), 0, e.text, e.textLayer));
            e.selected = {e.drawing.entities.size() - 1};
            e.pressed = false;
        } else if (e.tool == Tool::Measure) {
            if (e.measure.size() >= 2) e.measure.clear();
            e.measure.push_back(snap ? *snap : cursor);
            e.pressed = false;
        }
    }
    if (e.pressed && ImGui::IsMouseDown(ImGuiMouseButton_Left) && e.tool == Tool::Select) {
        const float moved = std::hypot(mouse.x - e.pressAt.x, mouse.y - e.pressAt.y);
        if (!e.dragging && !e.boxing && moved > ui.S(4)) {
            // Dragging a selected entity moves the selection; elsewhere it draws a box.
            const bool onSelection = [&] {
                for (const std::size_t i : e.selected)
                    if (dxf::Distance(e.drawing.entities[i], e.pressModel) < ui.S(6) / e.scale) return true;
                return false;
            }();
            if (onSelection) {
                e.dragging = true;
                e.beforeDrag = e.drawing;
            } else {
                e.boxing = true;
            }
        }
        if (e.dragging) {
            const Vec2 by = cursor - toModel({mouse.x - io.MouseDelta.x, mouse.y - io.MouseDelta.y});
            for (const std::size_t i : e.selected) dxf::Move(e.drawing.entities[i], by);
        }
        if (e.boxing) {
            const bool window = mouse.x >= e.pressAt.x;   // left to right: whole inside; right to left: touching
            const ImVec2 a{std::min(e.pressAt.x, mouse.x), std::min(e.pressAt.y, mouse.y)}, b{std::max(e.pressAt.x, mouse.x), std::max(e.pressAt.y, mouse.y)};
            const Color tone = window ? c.accent : c.success;
            dl->AddRectFilled(a, b, ui::ToU32(tone.WithAlpha(0.15f)));
            dl->AddRect(a, b, ui::ToU32(tone.WithAlpha(0.8f)), 0, 0, ui.S(1));
        }
    }
    if (e.pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (e.dragging && e.beforeDrag) {
            e.undo.push_back(std::move(*e.beforeDrag));
            e.redo.clear();
            e.dirty = true;
            e.check.reset();
        } else if (e.boxing) {
            const bool window = mouse.x >= e.pressAt.x;
            const Vec2 m1 = toModel(e.pressAt), m2 = cursor;
            Box2 sel;
            sel.Include(m1), sel.Include(m2);
            if (!io.KeyShift) e.selected.clear();
            for (std::size_t i = 0; i < e.drawing.entities.size(); ++i) {
                const Entity& en = e.drawing.entities[i];
                if (!LayerVisible(e, en)) continue;
                const Box2 b = dxf::Bounds(en);
                const bool inside = b.min.x >= sel.min.x && b.max.x <= sel.max.x && b.min.y >= sel.min.y && b.max.y <= sel.max.y;
                const bool touching = b.max.x >= sel.min.x && b.min.x <= sel.max.x && b.max.y >= sel.min.y && b.min.y <= sel.max.y &&
                                      [&] {
                                          for (const Vec2 v : dxf::Outline(en, 5.0))
                                              if (v.x >= sel.min.x && v.x <= sel.max.x && v.y >= sel.min.y && v.y <= sel.max.y) return true;
                                          return false;
                                      }();
                if (window ? inside : touching) e.selected.insert(i);
            }
        } else if (e.tool == Tool::Select && !under && !io.KeyShift) {
            e.selected.clear();   // a click on empty space
        }
        e.pressed = e.dragging = e.boxing = false;
        e.beforeDrag.reset();
    }

    // Measure: the line and the numbers.
    if (!e.measure.empty()) {
        const ImU32 mc = ui::ToU32(c.success);
        const Vec2 last = e.measure.size() == 2 ? e.measure[1] : (snap ? *snap : cursor);
        dl->AddLine(toScreen(e.measure[0]), toScreen(last), mc, ui.S(1.5f));
        dl->AddCircleFilled(toScreen(e.measure[0]), ui.S(3), mc);
        const std::string label = std::format("{:.2f} мм", (last - e.measure[0]).Length());
        const ImVec2 m = toScreen((e.measure[0] + last) * 0.5);
        dl->AddText(ui.fonts.mono, ui.S(13), {m.x + ui.S(6), m.y - ui.S(18)}, mc, label.c_str());
    }

    // Keys while the drawing is under the mouse.
    if (!typing && (hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected(e);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo(e);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) Redo(e);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save(app, e);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            for (std::size_t i = 0; i < e.drawing.entities.size(); ++i)
                if (LayerVisible(e, e.drawing.entities[i])) e.selected.insert(i);
        }
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) e.fitPending = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            e.selected.clear();
            e.measure.clear();
            e.tool = Tool::Select;
        }
    }

    // Cursor position and counts.
    const std::string status = std::format("X {:.2f}  Y {:.2f}  ·  {}", cursor.x, cursor.y,
                                           Trf("объектов {}, выбрано {}", e.drawing.entities.size(), e.selected.size()));
    dl->AddText(ui.fonts.mono, ui.S(12), {p0.x + ui.S(10), p0.y + size.y - ui.S(20)}, ui::ToU32(c.textSecondary), status.c_str());
    if (!e.skippedNote.empty())
        dl->AddText(ui.fonts.caption, ui.S(11), {p0.x + ui.S(10), p0.y + ui.S(8)}, ui::ToU32(c.warning), e.skippedNote.c_str());
    dl->PopClipRect();
}

} // namespace

// --- public ---------------------------------------------------------------------

void OpenDxfEditor(AppContext&, dxf::Drawing drawing, std::string title, std::filesystem::path path) {
    Editor& e = The();
    e = Editor{};
    e.open = true;
    e.drawing = std::move(drawing);
    e.drawing.EnsureLayer(dxf::kCut, 7);
    e.drawing.EnsureLayer(dxf::kBend, 1);
    e.drawing.EnsureLayer(dxf::kMark, 3);
    e.title = std::move(title);
    e.path = std::move(path);
}

bool OpenDxfEditorFile(AppContext& app, const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = Trf("Не удалось открыть {}", ToUtf8(path.filename()));
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    dxf::ReadReport report;
    Expected<Drawing> d = dxf::Read(buffer.str(), &report);
    if (!d) {
        if (error) *error = d.GetError().message;
        return false;
    }
    OpenDxfEditor(app, std::move(*d), ToUtf8(path.filename()), path);
    std::string note;
    for (const auto& [kind, count] : report.skipped) note += (note.empty() ? "" : ", ") + std::format("{} {}", kind, count);
    if (!note.empty()) The().skippedNote = Trf("Не показано (не нужно для резки): {}", note);
    return true;
}

dxf::Drawing* DxfEditorDrawing() { return The().open ? &The().drawing : nullptr; }

bool SaveDxfEditor(AppContext&, const std::filesystem::path& path, std::string* error) {
    if (!The().open) {
        if (error) *error = std::string(Tr("Редактор DXF не открыт"));
        return false;
    }
    return SaveTo(The(), path, error);
}

std::size_t SelectInDxfEditor(std::string_view layer) {
    Editor& e = The();
    e.selected.clear();
    for (std::size_t i = 0; i < e.drawing.entities.size(); ++i)
        if (layer.empty() || e.drawing.entities[i].layer == layer) e.selected.insert(i);
    return e.selected.size();
}

dxf::Check CheckInDxfEditor() {
    Editor& e = The();
    e.checkLayer.clear();
    for (const Entity& en : e.drawing.entities)
        if (en.layer == dxf::kCut) e.checkLayer = dxf::kCut;
    e.check = dxf::CheckForCutting(e.drawing, e.checkLayer);
    return *e.check;
}

void DrawDxfEditor(AppContext& app) {
    Editor& e = The();
    if (!e.open) return;
    ui::Ui& ui = app.ui;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const char* id = "##dxf-editor";
    if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    const float margin = ui.S(app.shell.mobile.phone ? 8.0f : 32.0f);
    ImGui::SetNextWindowSize({display.x - margin, display.y - margin}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(16, 14));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui.S(8, 6));
    const bool visible = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    if (!visible) return;

    const float width = ImGui::GetContentRegionAvail().x;
    // The phone (or a narrow window): icons, and the drawing or the panels in turn.
    const bool compact = app.shell.mobile.phone || width < ui.S(900);
    if (compact) DrawCompactToolbar(app, e, width);
    else DrawToolbar(app, e, width);
    ImGui::Dummy({0, ui.S(4)});
    const float left = compact ? width : ui.S(270);
    const float height = ImGui::GetContentRegionAvail().y;
    if (!compact || e.sidePanel) {
        ImGui::BeginChild("##dxf-side", {left, height}, ImGuiChildFlags_None, compact ? 0 : ImGuiWindowFlags_NoScrollbar);
        const float w = left - ui.S(8);
        DrawToolPanel(app, e, w);
        DrawLayers(app, e, w);
        DrawSelection(app, e, w);
        DrawCheckPanel(app, e, w);
        ImGui::EndChild();
    }
    if (!compact) ImGui::SameLine(0, ui.S(10));
    if (!compact) DrawCanvas(app, e, {width - left - ui.S(10), height});
    else if (!e.sidePanel) DrawCanvas(app, e, {width, height});

    // Closing with unsaved changes.
    if (e.confirmClose) {
        ImGui::OpenPopup("##dxf-unsaved");
        e.confirmClose = false;
    }
    if (ImGui::BeginPopupModal("##dxf-unsaved", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration)) {
        ImGui::TextUnformatted(Tr("В чертеже есть несохранённые изменения."));
        const ImVec2 bs{ui.S(130), ui.S(32)};
        if (ui::PrimaryButton(ui, "##u-save", Tr("Сохранить"), nullptr, bs)) {
            Save(app, e);
            if (!e.dirty) e.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton(ui, "##u-drop", Tr("Не сохранять"), nullptr, bs)) {
            e.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton(ui, "##u-cancel", Tr("Отмена"), nullptr, bs)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!e.open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace bz::app
