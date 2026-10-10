#include "app/features/AppearanceEditor.h"

#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "core/PathText.h"
#include "document/Commands.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include "core/Image.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <format>
#include <fstream>
#include <iterator>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bz::app {
namespace {

void Section(AppContext& app, const char* label) {
    ui::FieldLabel(app.ui, label);
    ImGui::Dummy({0, app.ui.S(2)});
}

void Caption(AppContext& app, const char* text, float width) {
    ui::FontScope font(app.ui.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(app.ui.C().textMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

/// The standard names are translated; the user's own are shown as typed.
std::string NameOf(const doc::Appearance& a) { return a.id.starts_with("std.") ? Tr(a.name) : a.name; }

/// A sample of the appearance: its colour (over a checkerboard where it is
/// see-through), a shine for metal, a glint for glass.
void DrawSample(ImDrawList* dl, ImVec2 p, float size, const doc::Appearance* a, const ui::ThemeColors& c, float rounding,
                unsigned picture = 0) {
    const ImVec2 q{p.x + size, p.y + size};
    if (a && picture) {   // a picture: a piece of it
        dl->AddImageRounded(static_cast<ImTextureID>(picture), p, q, {0, 0}, {1, 1}, IM_COL32_WHITE, rounding);
        dl->AddRect(p, q, ui::ToU32(a->edges.WithAlpha(0.8f)), rounding, 0, 1.0f);
        return;
    }
    if (!a) {   // the default look: the theme's body colour, crossed out
        dl->AddRectFilled(p, q, ui::ToU32(c.body), rounding);
        dl->AddLine({p.x + size * 0.2f, q.y - size * 0.2f}, {q.x - size * 0.2f, p.y + size * 0.2f}, ui::ToU32(c.textMuted), 1.5f);
        return;
    }
    if (a->face.a < 0.999f) {
        const float cell = size / 4;
        dl->AddRectFilled(p, q, IM_COL32(255, 255, 255, 255), rounding);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if ((i + j) % 2)
                    dl->AddRectFilled({p.x + i * cell, p.y + j * cell}, {p.x + (i + 1) * cell, p.y + (j + 1) * cell}, IM_COL32(200, 204, 210, 255));
    }
    const Color f = a->face;
    if (a->finish == doc::Finish::Metal) {
        const auto shade = [&](float k) {
            return ImGui::ColorConvertFloat4ToU32({std::min(1.0f, f.r * k), std::min(1.0f, f.g * k), std::min(1.0f, f.b * k), f.a});
        };
        dl->AddRectFilledMultiColor(p, q, shade(1.25f), shade(1.25f), shade(0.7f), shade(0.7f));
    } else {
        dl->AddRectFilled(p, q, ui::ToU32(f), rounding);
    }
    if (a->finish == doc::Finish::Glass)
        dl->AddLine({p.x + size * 0.25f, p.y + size * 0.65f}, {p.x + size * 0.65f, p.y + size * 0.25f}, IM_COL32(255, 255, 255, 170), 2.0f);
    dl->AddRect(p, q, ui::ToU32(a->edges.WithAlpha(0.8f)), rounding, 0, 1.0f);
}

} // namespace

AppearanceEditor::AppearanceEditor() : myLibrary(platform::ConfigDir() / "appearances") {}

void AppearanceEditor::Start(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info(Tr("Сначала создайте тело: внешний вид задаётся телам и граням"));
        return;
    }
    app.ops.Close(app, session);
    if (session.mode == EditMode::Sketch) app.sketch.Finish(app, session);
    myBefore = session.doc.look;
    myLibrary.Reload();
    myOpen = true;
}

std::string AppearanceEditor::Hint() const {
    return myFaces ? Tr("Выберите внешний вид и щёлкните грань модели. Enter — готово, Esc — отмена")
                   : Tr("Выберите внешний вид и щёлкните тело. Enter — готово, Esc — отмена");
}

const doc::Appearance* AppearanceEditor::Chosen() const {
    if (myChosen.empty()) return nullptr;
    if (const doc::Appearance* a = doc::FindStandard(myChosen)) return a;
    return myLibrary.Find(myChosen);
}

void AppearanceEditor::Done(AppContext& app, Session& session) {
    (void)app;
    if (!myOpen) return;
    myOpen = false;
    if (session.doc.look == myBefore) return;
    // One undo step: the look as it was goes back first, the command brings the new one.
    doc::Look now = session.doc.look;
    session.doc.look = myBefore;
    session.Execute(doc::SetLook(std::move(now), Tr("Внешний вид")));
}

void AppearanceEditor::Cancel(AppContext& app, Session& session) {
    (void)app;
    if (!myOpen) return;
    myOpen = false;
    if (session.doc.look == myBefore) return;
    session.doc.look = myBefore;
    session.doc.TouchLook();
}

void AppearanceEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    const auto hit = app.viewport.Pick(vf.ToLocal(ImGui::GetMousePos()));
    if (!hit || !hit->body) return;
    if (myFaces) {
        if (!hit->face) return;
        // The same face clicked elsewhere: its earlier colour goes.
        std::erase_if(session.doc.look.faces, [&](const doc::Look::FaceLook& f) {
            return f.body == hit->body && app.viewport.SameFace(hit->body, f.face, *hit->face);
        });
        session.doc.look.SetFace(hit->body, *hit->face, Chosen());
    } else {
        session.doc.look.SetBody(hit->body, Chosen());
    }
    // A picture goes into the part's file with it.
    if (const doc::Appearance* a = Chosen(); a && a->HasPicture() && !session.doc.look.pictures.contains(a->picture))
        session.doc.look.pictures[a->picture] = myLibrary.PictureBytes(a->picture);
    session.doc.TouchLook();
}

bool AppearanceEditor::Swatch(AppContext& app, const doc::Appearance* a, const std::string& label, float size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const std::string id = a ? a->id : std::string("default");
    ImGui::PushID(id.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##swatch", {size, size});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = ui.S(4);
    DrawSample(dl, {p.x + pad, p.y + pad}, size - 2 * pad, a, c, ui.S(6), ThumbnailOf(a));
    const bool chosen = (a ? a->id : std::string()) == myChosen;
    if (chosen) dl->AddRect(p, {p.x + size, p.y + size}, ui::ToU32(c.accent), ui.S(9), 0, ui.S(2));
    else if (hovered) dl->AddRect(p, {p.x + size, p.y + size}, ui::ToU32(c.fieldBorder), ui.S(9), 0, ui.S(1));
    if (hovered) ui::ItemTooltip(ui, label.c_str());
    ImGui::PopID();
    return clicked;
}

void AppearanceEditor::DrawLibrary(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    const float size = ui.S(46);
    const int perRow = std::max(1, static_cast<int>((width + ui.S(4)) / (size + ui.S(4))));
    int column = 0;
    const auto next = [&] {
        if (++column < perRow) ImGui::SameLine(0, ui.S(4));
        else column = 0;
    };

    Section(app, Tr("Стандартные"));
    column = 0;
    if (Swatch(app, nullptr, Tr("По умолчанию (цвет темы)"), size)) myChosen.clear();
    next();
    for (const doc::Appearance& a : doc::StandardAppearances()) {
        if (Swatch(app, &a, NameOf(a), size)) myChosen = a.id;
        next();
    }
    if (column != 0) ImGui::NewLine();
    ImGui::Dummy({0, ui.S(10)});

    Section(app, Tr("Мои"));
    column = 0;
    std::string removeId;
    for (const doc::Appearance& a : myLibrary.Items()) {
        if (Swatch(app, &a, NameOf(a), size)) myChosen = a.id;
        // Right click: change or delete.
        if (ImGui::BeginPopupContextItem(("##own-" + a.id).c_str())) {
            ui::MenuStyle style(ui);
            if (ui::MenuItem(ui, Tr("Изменить…"))) {
                myDraft = a;
                myKind = a.HasPicture() ? 1 : 0;
                myPictureFile.clear();
                myCreating = true;
            }
            if (ui::MenuItem(ui, Tr("Удалить"))) removeId = a.id;
            ImGui::EndPopup();
        }
        next();
    }
    // "+": a new one.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##new-look", {size, size})) {
            myDraft = {};
            myKind = 0;
            myPictureFile.clear();
            myDraft.name = Tr("Мой цвет");
            if (const doc::Appearance* base = Chosen(); base && !base->HasPicture()) {
                myDraft.face = base->face;
                myDraft.edges = base->edges;
                myDraft.finish = base->finish;
            }
            myCreating = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 q{p.x + size, p.y + size};
        dl->AddRect({p.x + ui.S(4), p.y + ui.S(4)}, {q.x - ui.S(4), q.y - ui.S(4)}, ui::ToU32(hovered ? ui.C().accent : ui.C().fieldBorder),
                    ui.S(6), 0, ui.S(1.2f));
        const ImVec2 m{(p.x + q.x) / 2, (p.y + q.y) / 2};
        const ImU32 ink = ui::ToU32(hovered ? ui.C().accent : ui.C().textMuted);
        dl->AddLine({m.x - ui.S(7), m.y}, {m.x + ui.S(7), m.y}, ink, ui.S(1.6f));
        dl->AddLine({m.x, m.y - ui.S(7)}, {m.x, m.y + ui.S(7)}, ink, ui.S(1.6f));
        if (hovered) ui::ItemTooltip(ui, Tr("Создать свой внешний вид"));
    }
    if (!removeId.empty()) {
        if (myChosen == removeId) myChosen = "std.steel";
        if (const Status s = myLibrary.Remove(removeId); !s) app.notes.Error(Tr(s.GetError().message));
    }
    ImGui::Dummy({0, ui.S(4)});
    if (ui::LinkButton(ui, "##import-looks", Tr("Импорт…"))) {
        if (const auto file = platform::ShowOpenDialog(Tr("Импорт внешних видов"), {{Tr("Внешние виды"), "*.zip *.json"}}, platform::DocumentsDir())) {
            if (const auto added = myLibrary.Import(*file)) app.notes.Success(Trf("Добавлено в «Мои»: {}", *added));
            else app.notes.Error(Tr(added.GetError().message));
        }
    }
    if (!myLibrary.Items().empty()) {
        ImGui::SameLine(0, ui.S(14));
        if (ui::LinkButton(ui, "##export-looks", Tr("Экспорт «Моих»…"))) {
            if (const auto file = platform::ShowSaveDialog(Tr("Экспорт внешних видов"), platform::DocumentsDir() / "appearances.zip",
                                                           {{Tr("Пакет внешних видов"), "*.zip"}}, ".zip")) {
                if (const Status s = myLibrary.Export(myLibrary.Items(), *file); s) app.notes.Success(Tr("Внешние виды сохранены"));
                else app.notes.Error(Tr(s.GetError().message));
            }
        }
    }
}

unsigned AppearanceEditor::Thumbnail(const std::string& key, const std::vector<std::uint8_t>& bytes) {
    if (const auto it = myThumbnails.find(key); it != myThumbnails.end()) return it->second;
    GLuint texture = 0;
    if (const auto image = DecodeImage(bytes)) {
        // A small copy is plenty for a swatch (nearest pixels).
        const int side = 128;
        std::vector<std::uint8_t> small(static_cast<std::size_t>(side) * side * 4);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const int sx = x * image->width / side, sy = y * image->height / side;
                std::memcpy(&small[(static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x)) * 4],
                            &image->rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(image->width) + static_cast<std::size_t>(sx)) * 4], 4);
            }
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, small.data());
    }
    myThumbnails[key] = texture;
    return texture;
}

unsigned AppearanceEditor::ThumbnailOf(const doc::Appearance* a) {
    if (!a || !a->HasPicture()) return 0;
    if (const auto it = myThumbnails.find(a->picture); it != myThumbnails.end()) return it->second;
    if (a->picture.starts_with("draft:")) return 0;
    return Thumbnail(a->picture, myLibrary.PictureBytes(a->picture));
}

void AppearanceEditor::DrawCreateDialog(AppContext& app, Session& session) {
    (void)session;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const char* id = "##appearance-create";
    if (myCreating && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x / 2, display.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({ui.S(560), 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(24, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(14));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui.S(8, 8));
    const bool visible = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    if (!visible) {
        myCreating = false;
        return;
    }
    const float width = ImGui::GetContentRegionAvail().x;
    {
        ui::FontScope font(ui.fonts.title);
        ImGui::TextUnformatted(myDraft.id.empty() ? Tr("Новый внешний вид") : Tr("Изменить внешний вид"));
    }
    ImGui::Dummy({0, ui.S(4)});
    Section(app, Tr("Название"));
    ui::TextField(ui, "##look-name", myDraft.name, width);
    ImGui::Dummy({0, ui.S(6)});

    static constexpr std::array<const char*, 2> kinds{N_("Однородный цвет"), N_("Картинка")};
    myKind = ui::Segmented(ui, "look-kind", kinds, myKind, 2, width);
    ImGui::Dummy({0, ui.S(6)});

    // Left: the colour (or the picture) of the faces; right: preview, edges, finish.
    const float left = ui.S(270);
    ImGui::BeginGroup();
    if (myKind == 1) {
        Section(app, Tr("Картинка"));
        if (ui::SecondaryButton(ui, "##pick-picture", Tr("Выбрать картинку…"), nullptr, {left, ui.S(34)})) {
            if (const auto file = platform::ShowOpenDialog(Tr("Картинка для поверхности"),
                                                           {{Tr("Картинки"), "*.png *.jpg *.jpeg *.bmp *.tga"}}, platform::DocumentsDir())) {
                std::ifstream in(*file, std::ios::binary);
                const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                if (const auto image = DecodeImage(bytes)) {
                    myPictureFile = *file;
                    myDraft.picture = "draft:" + ToUtf8(*file);   // shown in the preview until saved
                    myThumbnails.erase(myDraft.picture);
                    Thumbnail(myDraft.picture, bytes);
                    // As it is: one pixel is a tenth of a millimetre on the part.
                    myDraft.pictureSize = std::max(1.0, image->width * 0.1);
                    myDraft.face = Color::Hex(0xFFFFFF, myDraft.face.a);
                    myDraft.edges = Color::Hex(0x000000);
                    if (myDraft.name.empty() || myDraft.name == Tr("Мой цвет")) myDraft.name = ToUtf8(file->stem());
                } else {
                    app.notes.Error(Tr(image.GetError().message));
                }
            }
        }
        if (myDraft.HasPicture()) {
            ImGui::Dummy({0, ui.S(4)});
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ui.S(150);
            if (const unsigned t = ThumbnailOf(&myDraft)) ImGui::GetWindowDrawList()->AddImageRounded(static_cast<ImTextureID>(t), p, {p.x + left, p.y + h}, {0, 0}, {left / h, 1}, IM_COL32_WHITE, ui.S(8));
            ImGui::Dummy({left, h});
            ImGui::Dummy({0, ui.S(4)});
            Section(app, Tr("Ширина одной копии на детали, мм"));
            std::string size = std::format("{:g}", myDraft.pictureSize);
            if (ui::TextField(ui, "##picture-size", size, left)) {
                double value = 0;
                if (std::sscanf(size.c_str(), "%lf", &value) == 1 && value > 0.01) myDraft.pictureSize = value;
            }
            Caption(app, Tr("Картинка повторяется без зазоров, огибает цилиндры и сферы. Сначала 1 пиксель = 0,1 мм."), left);
            Section(app, Tr("Прозрачность"));
            float transparency = 1.0f - myDraft.face.a;
            if (ui::Slider(ui, "##picture-alpha", transparency, left)) myDraft.face.a = 1.0f - transparency;
        } else {
            Caption(app, Tr("PNG, JPEG, BMP или TGA. Картинка копируется в библиотеку и сохраняется в файле детали."), left);
        }
        ImGui::EndGroup();
    } else {
    Section(app, Tr("Цвет граней"));
    float face[4] = {myDraft.face.r, myDraft.face.g, myDraft.face.b, myDraft.face.a};
    ImGui::SetNextItemWidth(left);
    if (ImGui::ColorPicker4("##face-colour", face,
                            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHex))
        myDraft.face = {face[0], face[1], face[2], face[3]};
    Caption(app, Tr("Правая полоса — прозрачность: ниже — прозрачнее (A: 255 — непрозрачный)."), left);
    ImGui::EndGroup();
    }
    ImGui::SameLine(0, ui.S(20));
    ImGui::BeginGroup();
    const float right = width - left - ui.S(20);
    Section(app, Tr("Образец"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        DrawSample(ImGui::GetWindowDrawList(), p, ui.S(96), &myDraft, c, ui.S(10), myKind == 1 ? ThumbnailOf(&myDraft) : 0u);
        ImGui::Dummy({ui.S(96), ui.S(96)});
    }
    ImGui::Dummy({0, ui.S(6)});
    Section(app, Tr("Цвет рёбер"));
    float edges[3] = {myDraft.edges.r, myDraft.edges.g, myDraft.edges.b};
    ImGui::SetNextItemWidth(right);
    if (ImGui::ColorEdit3("##edge-colour", edges, ImGuiColorEditFlags_DisplayHex)) myDraft.edges = {edges[0], edges[1], edges[2], 1.0f};
    ImGui::Dummy({0, ui.S(6)});
    Section(app, Tr("Поверхность"));
    static constexpr std::array<const char*, 3> finishes{N_("Матовая"), N_("Металл"), N_("Стекло")};
    const int finish = static_cast<int>(myDraft.finish);
    if (const int f = ui::Segmented(ui, "finish", finishes, finish, 1, right); f != finish) myDraft.finish = static_cast<doc::Finish>(f);
    ImGui::EndGroup();

    ImGui::Dummy({0, ui.S(10)});
    const float bw = (width - ui.S(8)) / 2;
    if (ui::SecondaryButton(ui, "##look-cancel", Tr("Отмена"), nullptr, {bw, ui.S(36)})) {
        myCreating = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0, ui.S(8));
    const bool ready = !myDraft.name.empty() && (myKind == 0 || myDraft.HasPicture());
    if (ui::PrimaryButton(ui, "##look-save", Tr("Сохранить в «Мои»"), nullptr, {bw, ui.S(36)}, ready)) {
        doc::Appearance draft = myDraft;
        if (myKind == 0) draft.picture.clear();
        else if (!myPictureFile.empty()) draft.picture.clear();   // set by the library from the file
        if (const auto saved = myLibrary.Save(draft, myKind == 1 ? myPictureFile : std::filesystem::path{})) {
            myChosen = saved->id;
            if (saved->HasPicture()) myThumbnails.erase(saved->picture);   // a new picture under the same name
            // Already on the part: it takes the changed definition (and picture).
            doc::Look& look = app.Doc()->doc.look;
            if (look.Refresh(*saved)) {
                if (saved->HasPicture()) look.pictures[saved->picture] = myLibrary.PictureBytes(saved->picture);
                look.Prune();
                app.Doc()->doc.TouchLook();
            }
        } else {
            app.notes.Error(Tr(saved.GetError().message));
        }
        myCreating = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AppearanceEditor::DrawPanel(AppContext& app, Session& session) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float width = ImGui::GetContentRegionAvail().x;

    // Header: icon, title, close.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        ui.icons.Draw(dl, "appearance", {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        dl->AddText(ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, ui::ToU32(c.text), Title());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##close-look", "ui-close", 30, 14, Tr("Отмена (Esc)"))) app.Later([&app, &session, this] { Cancel(app, session); });
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }

    const float footer = ui.S(62);
    ImGui::BeginChild("##look-body", {width, ImGui::GetContentRegionAvail().y - footer}, ImGuiChildFlags_None);
    ImGui::SetCursorPos({ui.S(14), ui.S(14)});
    ImGui::BeginGroup();
    const float inner = width - ui.S(28);

    Section(app, Tr("Щелчок красит"));
    static constexpr std::array<const char*, 2> targets{N_("Тело целиком"), N_("Грань")};
    const int target = myFaces ? 1 : 0;
    if (const int t = ui::Segmented(ui, "look-target", targets, target, 2, inner); t != target) myFaces = t == 1;
    Caption(app, myFaces ? Tr("Щелчок по грани красит только её; повторный щелчок другим цветом перекрашивает.")
                         : Tr("Щелчок по телу красит его целиком (свои цвета граней этого тела сбрасываются)."),
            inner);
    ImGui::Dummy({0, ui.S(10)});

    // What a click puts on.
    {
        const doc::Appearance* a = Chosen();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + inner, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        DrawSample(dl, {p.x + ui.S(8), p.y + ui.S(8)}, h - ui.S(16), a, c, ui.S(5));
        const std::string name = a ? NameOf(*a) : std::string(Tr("По умолчанию"));
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + h + ui.S(4), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), name.c_str());
        ImGui::Dummy({inner, h});
    }
    ImGui::Dummy({0, ui.S(10)});

    DrawLibrary(app, inner);

    if (!session.doc.look.Empty()) {
        ImGui::Dummy({0, ui.S(10)});
        if (ui::LinkButton(ui, "##reset-look", Tr("Сбросить все цвета детали"))) {
            session.doc.look = {};
            session.doc.TouchLook();
        }
    }
    ImGui::EndGroup();
    ImGui::EndChild();

    // Footer: Отмена / Готово.
    ui::Divider(ui, c.dividerSoft);
    const ImVec2 fp = ImGui::GetCursorScreenPos();
    const float bw = (width - ui.S(28) - ui.S(8)) / 2.0f;
    const float bh = ui.S(36);
    ImGui::SetCursorScreenPos({fp.x + ui.S(14), fp.y + ui.S(12)});
    if (ui::SecondaryButton(ui, "##look-cancel-op", Tr("Отмена"), nullptr, {bw, bh})) app.Later([&app, &session, this] { Cancel(app, session); });
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##look-done", Tr("Готово"), "Enter", {bw, bh})) app.Later([&app, &session, this] { Done(app, session); });

    DrawCreateDialog(app, session);
}

} // namespace bz::app
