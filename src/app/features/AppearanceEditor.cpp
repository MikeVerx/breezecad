#include "app/features/AppearanceEditor.h"
#include "app/features/ToolChoice.h"

#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "core/PathText.h"
#include "core/Version.h"
#include "core/Monitor.h"
#include "app/AssemblyState.h"
#include "app/Session.h"
#include "assembly/AssemblyFile.h"
#include "document/BzcFile.h"
#include "document/Commands.h"
#include "platform/FileDialogs.h"
#include "platform/Paths.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include "core/Image.h"

#include "ui/Gl.h"
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

/// What a part file written from here says made it.
std::string PartGenerator() { return std::string("BreezeCAD ") + Version(); }

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
    myBefore = LookOf(session);
    myLibrary.Reload();
    myTargets.clear();
    myTargetBodies.clear();
    myEdits.clear();
    myFaces = false;   // a whole object by default
    // In an assembly the component chosen there is what the library paints.
    if (session.assembly && session.assembly->assembly.FindComponent(session.assembly->selected))
        myTargetBodies.push_back(session.assembly->selected);
    myTargetsChosen = myChosen;   // nothing goes on them until a choice is made
    myOpen = true;
}

void AppearanceEditor::StartOnFaces(AppContext& app, Session& session, std::vector<render::SelectedFace> faces) {
    Start(app, session);
    if (!myOpen || faces.empty()) return;
    myFaces = true;
    myTargets = std::move(faces);
}

doc::Look& AppearanceEditor::LookOf(Session& session) {
    return session.assembly ? session.assembly->assembly.look : session.doc.look;
}

void AppearanceEditor::Touch(Session& session) {
    if (session.assembly) session.assembly->assembly.TouchLook();
    else session.doc.TouchLook();
}

doc::FaceReference AppearanceEditor::Stored(Session& session, ObjectId body, doc::FaceReference shown) {
    if (!session.assembly) return shown;
    const assembly::Component* c = session.assembly->assembly.FindComponent(body);
    if (!c) return shown;
    const assembly::Placement back = c->placement.Inverse();
    shown.point = back.Point(shown.point);
    shown.normal = back.Direction(shown.normal);
    return shown;
}

doc::FaceReference AppearanceEditor::Shown(Session& session, ObjectId body, doc::FaceReference stored) {
    if (!session.assembly) return stored;
    const assembly::Component* c = session.assembly->assembly.FindComponent(body);
    if (!c) return stored;
    stored.point = c->placement.Point(stored.point);
    stored.normal = c->placement.Direction(stored.normal);
    return stored;
}

void AppearanceEditor::KeepPicture(Session& session) {
    doc::Look& look = LookOf(session);
    if (const doc::Appearance* a = Chosen(); a && a->HasPicture() && !look.pictures.contains(a->picture))
        look.pictures[a->picture] = myLibrary.PictureBytes(a->picture);
}

void AppearanceEditor::PaintBody(Session& session, ObjectId body) {
    mon::Action("appearance", "объект");
    LookOf(session).SetBody(body, Chosen());
    KeepPicture(session);
    Touch(session);
    if (session.assembly) myEdits.push_back({body, std::nullopt, myChosen});
}

void AppearanceEditor::PaintFace(AppContext& app, Session& session, ObjectId body, const doc::FaceReference& shown) {
    mon::Action("appearance", "грань");
    doc::Look& look = LookOf(session);
    // The same face set before (picked at another point of it): that look goes.
    std::erase_if(look.faces, [&](const doc::Look::FaceLook& f) {
        return f.body == body && app.viewport.SameFace(body, Shown(session, body, f.face), shown);
    });
    const doc::FaceReference stored = Stored(session, body, shown);
    look.SetFace(body, stored, Chosen());
    KeepPicture(session);
    Touch(session);
    if (session.assembly) myEdits.push_back({body, stored, myChosen});
}

void AppearanceEditor::ApplyToTargets(AppContext& app, Session& session) {
    if (myFaces) {
        for (const render::SelectedFace& t : myTargets) PaintFace(app, session, t.body, t.face);
        return;
    }
    // A whole object: the bodies of the faces chosen, and the component chosen.
    std::vector<ObjectId> bodies = myTargetBodies;
    for (const render::SelectedFace& t : myTargets)
        if (std::find(bodies.begin(), bodies.end(), t.body) == bodies.end()) bodies.push_back(t.body);
    for (const ObjectId body : bodies) PaintBody(session, body);
}

std::size_t AppearanceEditor::TargetCount() const {
    if (myFaces) return myTargets.size();
    std::vector<ObjectId> bodies = myTargetBodies;
    for (const render::SelectedFace& t : myTargets)
        if (std::find(bodies.begin(), bodies.end(), t.body) == bodies.end()) bodies.push_back(t.body);
    return bodies.size();
}

void AppearanceEditor::ResetFaces(AppContext& app, Session& session, const std::vector<render::SelectedFace>& faces) {
    if (session.assembly) return;   // faces are chosen this way in a part only
    doc::Look look = session.doc.look;
    for (const render::SelectedFace& t : faces)
        std::erase_if(look.faces, [&](const doc::Look::FaceLook& f) {
            return f.body == t.body && app.viewport.SameFace(t.body, f.face, t.face);
        });
    look.Prune();
    if (look != session.doc.look) session.Execute(doc::SetLook(std::move(look), Tr("Внешний вид")));
}

std::string AppearanceEditor::Hint() const {
    if (const std::size_t n = TargetCount(); n > 0)
        return myFaces ? Trf("Выберите внешний вид: он ляжет на выбранные грани ({}). Enter — готово, Esc — отмена", n)
                       : Trf("Выберите внешний вид: он ляжет на выбранные объекты ({}). Enter — готово, Esc — отмена", n);
    return myFaces ? Tr("Выберите внешний вид и щёлкните грань модели. Enter — готово, Esc — отмена")
                   : Tr("Выберите внешний вид и щёлкните тело. Enter — готово, Esc — отмена");
}

const doc::Appearance* AppearanceEditor::Chosen() const {
    if (myChosen.empty()) return nullptr;
    if (const doc::Appearance* a = doc::FindStandard(myChosen)) return a;
    return myLibrary.Find(myChosen);
}

void AppearanceEditor::Close(AppContext& app) {
    myOpen = false;
    myTargets.clear();
    myTargetBodies.clear();
    app.viewport.SetHoveredBody({});
}

void AppearanceEditor::Done(AppContext& app, Session& session) {
    if (!myOpen) return;
    Close(app);
    std::vector<Edit> edits = std::move(myEdits);
    myEdits.clear();
    doc::Look& look = LookOf(session);
    if (look == myBefore) return;
    if (session.assembly) {
        AssemblyState& st = *session.assembly;
        doc::Look painted = std::move(look);
        look = myBefore;
        if (myToPart) {
            // The looks go into the parts' files. The assembly keeps its
            // own overrides, except where they would hide what was just
            // written (a reset in the panel clears them all).
            doc::Look kept = painted.Empty() ? doc::Look{} : myBefore;
            for (const Edit& e : edits) {
                if (e.face) kept.SetFace(e.component, *e.face, nullptr);
                else kept.SetBody(e.component, nullptr);
            }
            if (kept != myBefore) {
                st.Checkpoint(Tr("Внешний вид"));
                look = std::move(kept);
                look.pictures = myBefore.pictures;
            }
            st.assembly.TouchLook();
            WriteIntoParts(app, session, painted, edits);
            return;
        }
        st.Checkpoint(Tr("Внешний вид"));   // remembers it as it was: one undo step
        look = std::move(painted);
        st.assembly.TouchLook();
        return;
    }
    // One undo step: the look as it was goes back first, the command brings the new one.
    doc::Look now = std::move(look);
    look = myBefore;
    session.Execute(doc::SetLook(std::move(now), Tr("Внешний вид")));
}

void AppearanceEditor::Cancel(AppContext& app, Session& session) {
    if (!myOpen) return;
    Close(app);
    myEdits.clear();
    doc::Look& look = LookOf(session);
    if (look == myBefore) return;
    look = myBefore;
    Touch(session);
}

void AppearanceEditor::WriteIntoParts(AppContext& app, Session& session, const doc::Look& painted, const std::vector<Edit>& edits) {
    AssemblyState& st = *session.assembly;
    // The edits of each part file, in the order they were made (the last wins).
    std::vector<std::pair<std::filesystem::path, std::vector<const Edit*>>> files;
    for (const Edit& e : edits) {
        const assembly::Component* c = st.assembly.FindComponent(e.component);
        if (!c) continue;
        const std::filesystem::path file = assembly::ResolvePartPath(session.path, c->path);
        auto it = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.first == file; });
        if (it == files.end()) it = files.insert(files.end(), {file, {}});
        it->second.push_back(&e);
    }
    const auto apply = [&](doc::Look& target, const std::vector<const Edit*>& list) {
        for (const Edit* e : list) {
            const doc::Appearance* a = e->appearance.empty() ? nullptr : painted.Find(e->appearance);
            const kernel::Model* part = st.geometry.PartModelOf(e->component);
            if (!part) continue;
            if (!e->face) {
                for (const kernel::BodyInfo& b : part->Bodies()) target.SetBody(b.id, a);
            } else if (const ObjectId body = part->BodyNearest(e->face->point)) {
                target.SetFace(body, *e->face, a);
            }
            if (a && a->HasPicture())
                if (const auto it = painted.pictures.find(a->picture); it != painted.pictures.end()) target.pictures[a->picture] = it->second;
        }
        target.Prune();
    };
    int written = 0;
    for (const auto& [file, list] : files) {
        const std::string name = ToUtf8(file.stem());
        // Open in a tab: the change goes there (with its own undo), saved
        // at once unless the tab has changes of its own.
        Session* open = nullptr;
        for (const auto& other : app.workspace.sessions)
            if (!other->assembly && !other->path.empty() && other->path == file) open = other.get();
        if (open) {
            const bool hadChanges = open->IsModified();
            doc::Look look = open->doc.look;
            apply(look, list);
            if (look == open->doc.look) continue;
            open->Execute(doc::SetLook(std::move(look), Tr("Внешний вид")));
            if (hadChanges) {
                app.notes.Info(Trf("Деталь «{}» открыта и не сохранена: вид добавлен в неё, сохраните её", name));
                continue;
            }
            if (const Status s = open->WriteFile(file, PartGenerator()); !s) {
                app.notes.Error(Tr(s.GetError().message));
                continue;
            }
            open->MarkSaved();
            ++written;
            continue;
        }
        Expected<doc::Document> part = doc::LoadBzc(file);
        if (!part) {
            app.notes.Error(Trf("Не удалось открыть деталь «{}»: {}", name, Tr(part.GetError().message)));
            continue;
        }
        apply(part->look, list);
        if (const Status s = doc::SaveBzc(*part, file, PartGenerator()); !s) {
            app.notes.Error(Tr(s.GetError().message));
            continue;
        }
        ++written;
    }
    // The assembly reads the changed files again on its own (they are newer).
    if (written > 0) app.notes.Success(Trf("Внешний вид записан в детали: {}", written));
}

void AppearanceEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    const std::optional<render::PickResult> hit =
        vf.hovered ? app.viewport.Pick(vf.ToLocal(ImGui::GetMousePos())) : std::nullopt;
    // A whole object is painted: it is lit up as a whole under the cursor.
    app.viewport.SetHoveredBody(!myFaces && hit ? hit->body : ObjectId{});
    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (!hit || !hit->body) return;
    if (myFaces) {
        if (hit->face) PaintFace(app, session, hit->body, *hit->face);
    } else {
        PaintBody(session, hit->body);
    }
}

void AppearanceEditor::Sample(AppContext& app, ImDrawList* dl, ImVec2 p, float size, const doc::Appearance* a) {
    const ui::ThemeColors& c = app.ui.C();
    const ImVec2 q{p.x + size, p.y + size};
    const float r = size * 0.28f;
    const Color edges = a ? a->edges : c.bodyEdges;
    if (const unsigned picture = ThumbnailOf(a)) {
        dl->AddImageRounded(static_cast<ImTextureID>(picture), p, q, {0, 0}, {1, 1}, IM_COL32_WHITE, r);
    } else {
        const Color face = a ? a->face : c.body;
        if (face.a < 0.999f) {   // see-through: a checkerboard shows through
            dl->AddRectFilled(p, q, IM_COL32(255, 255, 255, 255), r);
            const float half = size / 2;
            dl->AddRectFilled({p.x + half, p.y}, {q.x, p.y + half}, IM_COL32(196, 200, 206, 255), r, ImDrawFlags_RoundCornersTopRight);
            dl->AddRectFilled({p.x, p.y + half}, {p.x + half, q.y}, IM_COL32(196, 200, 206, 255), r, ImDrawFlags_RoundCornersBottomLeft);
        }
        dl->AddRectFilled(p, q, ui::ToU32(face), r);
    }
    dl->AddRect(p, q, ui::ToU32(edges.WithAlpha(1.0f)), r, 0, app.ui.S(1.2f));
}

bool AppearanceEditor::Row(AppContext& app, const doc::Appearance* a, bool chosen, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const std::string id = a ? a->id : std::string("default");
    ImGui::PushID(id.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(28);
    const bool clicked = ImGui::InvisibleButton("##row", {width, h});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (chosen || hovered) dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(chosen ? c.accentSoft : c.hover), ui.S(6));
    const float s = ui.S(18);
    Sample(app, dl, {p.x + ui.S(6), p.y + (h - s) / 2}, s, a);
    const std::string name = a ? NameOf(*a) : std::string(Tr("По умолчанию"));
    dl->AddText(chosen ? ui.fonts.medium : ui.fonts.body, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)},
                ui::ToU32(c.text), name.c_str());
    ImGui::PopID();
    return clicked;
}

std::optional<std::string> AppearanceEditor::List(AppContext& app, std::string_view chosen, float width, bool withOwnMenu) {
    ui::Ui& ui = app.ui;
    std::optional<std::string> picked;
    Section(app, Tr("Стандартные"));
    if (Row(app, nullptr, chosen.empty(), width)) picked = std::string();
    for (const doc::Appearance& a : doc::StandardAppearances()) {
        if (Row(app, &a, chosen == a.id, width)) picked = a.id;
        // Right click: a look of one's own starting from this one (the
        // standard one stays as it is).
        if (!withOwnMenu) continue;
        ui::MenuStyle style(ui);   // before BeginPopup: it must outlive EndPopup
        if (ImGui::BeginPopupContextItem(("##std-" + a.id).c_str())) {
            if (ui::MenuItem(ui, Tr("Свой вид на его основе…"))) {
                myDraft = a;
                myDraft.id.clear();   // a new one in «Мои»
                myDraft.name = Trf("{} (мой)", NameOf(a));
                myKind = a.HasPicture() ? 1 : 0;
                myPictureFile.clear();
                myCreating = true;
            }
            ImGui::EndPopup();
        }
    }
    ImGui::Dummy({0, ui.S(8)});
    Section(app, Tr("Мои"));
    std::string removeId;
    for (const doc::Appearance& a : myLibrary.Items()) {
        if (Row(app, &a, chosen == a.id, width)) picked = a.id;
        // Right click: change or delete.
        if (!withOwnMenu) continue;
        ui::MenuStyle style(ui);   // before BeginPopup: it must outlive EndPopup
        if (ImGui::BeginPopupContextItem(("##own-" + a.id).c_str())) {
            if (ui::MenuItem(ui, Tr("Редактировать…"))) {
                myDraft = a;
                myKind = a.HasPicture() ? 1 : 0;
                myPictureFile.clear();
                myCreating = true;
            }
            if (ui::MenuItem(ui, Tr("Удалить"))) removeId = a.id;
            ImGui::EndPopup();
        }
    }
    if (myLibrary.Items().empty()) Caption(app, Tr("Пока нет: создайте свой цвет или картинку."), width);
    if (!removeId.empty()) {
        if (myChosen == removeId) myChosen = "std.steel";
        if (const Status s = myLibrary.Remove(removeId); !s) app.notes.Error(Tr(s.GetError().message));
    }
    return picked;
}

void AppearanceEditor::StartCreate() {
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

void AppearanceEditor::DrawPhonePanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Remember what was chosen, for «Недавние».
    if (myChosen != myRecentSeen) {
        myRecentSeen = myChosen;
        myRecent.erase(std::remove(myRecent.begin(), myRecent.end(), myChosen), myRecent.end());
        myRecent.insert(myRecent.begin(), myChosen);
        if (myRecent.size() > 6) myRecent.resize(6);
    }
    // A page over block 1: its name, ✕ in the top right corner (back to the main page).
    const auto head = [&](const char* title) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ui.fonts.strong, ui.S(14), {p.x, p.y + ui.S(9)}, ui::ToU32(c.text), title);
        ImGui::SetCursorScreenPos({p.x + width - ui.S(34), p.y});
        const bool closed = ui::IconButton(ui, "##page-close", "ui-close", 34, 16, Tr("Закрыть"));
        ImGui::SetCursorScreenPos({p.x, p.y + ui.S(40)});
        ui::Divider(ui, c.dividerSoft);
        ImGui::Dummy({0, ui.S(4)});
        return closed;
    };
    if (myPhonePage == 1 || myPhonePage == 2) {
        if (head(myPhonePage == 1 ? Tr("Библиотека") : Tr("Мои цвета"))) myPhonePage = 0;
        if (myPhonePage == 1) {
            if (Row(app, nullptr, myChosen.empty(), width)) myChosen.clear();
            for (const doc::Appearance& a : doc::StandardAppearances())
                if (Row(app, &a, myChosen == a.id, width)) myChosen = a.id;
        } else {
            for (const doc::Appearance& a : myLibrary.Items())
                if (Row(app, &a, myChosen == a.id, width)) myChosen = a.id;
            if (myLibrary.Items().empty()) Caption(app, Tr("Пока нет: создайте свой цвет или картинку."), width);
            ImGui::Dummy({0, ui.S(6)});
            if (ui::LinkButton(ui, "##import-looks-phone", Tr("Импорт…"))) {
                if (const auto file = platform::ShowOpenDialog(Tr("Импорт внешних видов"), {{Tr("Внешние виды"), "*.zip *.json"}}, platform::DocumentsDir())) {
                    if (const auto added = myLibrary.Import(*file)) app.notes.Success(Trf("Добавлено в «Мои»: {}", *added));
                    else app.notes.Error(Tr(added.GetError().message));
                }
            }
        }
        return;
    }
    if (myPhonePage == 3) {
        if (head(myDraft.id.empty() ? Tr("Новый цвет") : Tr("Изменить цвет"))) {
            myCreating = false;
            myPhonePage = 0;
            return;
        }
        myInline = true;
        DrawCreateBody(app, width);
        myInline = false;
        if (!myCreating) myPhonePage = 0;   // saved or cancelled
        return;
    }

    // The main page: what a tap on the model puts on now.
    {
        ui::FieldLabel(ui, Tr("Сейчас"));
        const doc::Appearance* a = Chosen();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(46);
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.segmentTrack), ui.S(10));
        Sample(app, dl, {p.x + ui.S(8), p.y + ui.S(8)}, h - ui.S(16), a);
        const std::string name = a ? NameOf(*a) : std::string(Tr("По умолчанию"));
        ui::FittedText(dl, ui.fonts.medium, ui.S(13), {p.x + h + ui.S(4), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, p.x + width - ui.S(6),
                       ui::ToU32(c.text), name.c_str());
        ImGui::Dummy({width, h});
    }
    DrawPictureSize(app, session, width);
    ImGui::Dummy({0, ui.S(6)});
    // The three ways to a look.
    struct Way {
        const char* icon;
        const char* label;
        int page;
    };
    const Way ways[] = {{"ui-folder", Tr("Из библиотеки"), 1}, {"ui-palette", Tr("Мои цвета"), 2}, {"ui-plus", Tr("Создать цвет"), 3}};
    for (const Way& w : ways) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(42);
        ImGui::PushID(w.page);
        const bool clicked = ImGui::InvisibleButton("##way", {width, h});
        ImGui::PopID();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(ImGui::IsItemActive() ? c.pressed : c.segmentTrack), ui.S(10));
        ui.icons.Draw(dl, w.icon, {p.x + ui.S(12), p.y + (h - ui.S(18)) / 2}, ui.S(18), c.textSecondary);
        ui::FittedText(dl, ui.fonts.medium, ui.S(13), {p.x + ui.S(40), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, p.x + width - ui.S(24),
                       ui::ToU32(c.text), w.label);
        ui.icons.Draw(dl, "ui-chevron-right", {p.x + width - ui.S(20), p.y + (h - ui.S(12)) / 2}, ui.S(12), c.textMuted);
        if (clicked) {
            if (w.page == 3) StartCreate();
            myPhonePage = w.page;
        }
        ImGui::Dummy({0, ui.S(4)});
    }
    // The recent looks: a tap takes one at once.
    if (myRecent.size() > 1) {
        ImGui::Dummy({0, ui.S(4)});
        ui::FieldLabel(ui, Tr("Недавние"));
        const float s = ui.S(30), gap = ui.S(8);
        ImVec2 at = ImGui::GetCursorScreenPos();
        const float x0 = at.x;
        for (std::size_t i = 0; i < myRecent.size(); ++i) {
            const doc::Appearance* a = myRecent[i].empty() ? nullptr : myLibrary.Find(myRecent[i]);
            if (!a && !myRecent[i].empty()) {
                for (const doc::Appearance& std : doc::StandardAppearances())
                    if (std.id == myRecent[i]) a = &std;
                if (!a) continue;
            }
            if (at.x + s > x0 + width) {
                at.x = x0;
                at.y += s + gap;
            }
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::InvisibleButton("##recent", {s, s})) myChosen = myRecent[i];
            ImGui::PopID();
            Sample(app, dl, at, s, a);
            if (myRecent[i] == myChosen) dl->AddRect({at.x - ui.S(2), at.y - ui.S(2)}, {at.x + s + ui.S(2), at.y + s + ui.S(2)}, ui::ToU32(c.accent), ui.S(8), 0, ui.S(2));
            at.x += s + gap;
        }
        ImGui::SetCursorScreenPos({x0, at.y + s + gap});
        ImGui::Dummy({width, 0});
    }
}

void AppearanceEditor::DrawLibrary(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    if (const auto picked = List(app, myChosen, width, true)) myChosen = *picked;
    ImGui::Dummy({0, ui.S(4)});
    if (ui::LinkButton(ui, "##new-look", Tr("+ Создать свой…"))) StartCreate();
    ImGui::SameLine(0, ui.S(14));
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

void AppearanceEditor::DrawCreateBody(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    (void)c;
    ImGui::Dummy({0, ui.S(4)});
    Section(app, Tr("Название"));
    ui::TextField(ui, "##look-name", myDraft.name, width);
    ImGui::Dummy({0, ui.S(6)});

    static constexpr std::array<const char*, 2> kinds{N_("Однородный цвет"), N_("Картинка")};
    myKind = ui::Segmented(ui, "look-kind", kinds, myKind, 2, width);
    ImGui::Dummy({0, ui.S(6)});

    // Left: the colour (or the picture) of the faces; right: preview, edges, finish.
    const float left = myInline ? width : ui.S(270);
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
    if (myInline) ImGui::Dummy({0, ui.S(8)});   // the phone: one column
    else ImGui::SameLine(0, ui.S(20));
    ImGui::BeginGroup();
    const float right = myInline ? width : width - left - ui.S(20);
    Section(app, Tr("Образец"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        DrawSample(ImGui::GetWindowDrawList(), p, ui.S(96), &myDraft, c, ui.S(10), myKind == 1 ? ThumbnailOf(&myDraft) : 0u);
        ImGui::Dummy({ui.S(96), ui.S(96)});
    }
    ImGui::Dummy({0, ui.S(6)});
    Section(app, Tr("Цвет рёбер (обводки)"));
    {
        // Quick choices, and a swatch that opens a full picker.
        const Color face = myDraft.face;
        const Color darker{face.r * 0.45f, face.g * 0.45f, face.b * 0.45f, 1.0f};
        const Color quick[] = {Color::Hex(0x000000), Color::Hex(0x3D4552), Color::Hex(0x9AA1AB), Color::Hex(0xFFFFFF), darker};
        const float s = ui.S(22);
        for (std::size_t i = 0; i < std::size(quick); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const ImVec4 v{quick[i].r, quick[i].g, quick[i].b, 1.0f};
            if (ImGui::ColorButton("##edge-quick", v, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, {s, s})) myDraft.edges = quick[i];
            if (i + 1 == std::size(quick)) ui::ItemTooltip(ui, Tr("Темнее цвета граней"));
            ImGui::PopID();
            ImGui::SameLine(0, ui.S(6));
        }
        const ImVec4 now{myDraft.edges.r, myDraft.edges.g, myDraft.edges.b, 1.0f};
        if (ImGui::ColorButton("##edge-swatch", now, ImGuiColorEditFlags_NoAlpha, {s * 2, s})) ImGui::OpenPopup("##edge-picker");
        ui::ItemTooltip(ui, Tr("Выбрать любой цвет"));
        if (ImGui::BeginPopup("##edge-picker")) {
            float edges[3] = {myDraft.edges.r, myDraft.edges.g, myDraft.edges.b};
            ImGui::SetNextItemWidth(ui.S(220));
            if (ImGui::ColorPicker3("##edge-colour", edges,
                                    ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB |
                                        ImGuiColorEditFlags_DisplayHex))
                myDraft.edges = {edges[0], edges[1], edges[2], 1.0f};
            ImGui::EndPopup();
        }
    }
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
        if (!myInline) ImGui::CloseCurrentPopup();
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
            doc::Look& look = LookOf(*app.Doc());
            if (look.Refresh(*saved)) {
                if (saved->HasPicture()) look.pictures[saved->picture] = myLibrary.PictureBytes(saved->picture);
                look.Prune();
                Touch(*app.Doc());
            }
        } else {
            app.notes.Error(Tr(saved.GetError().message));
        }
        myCreating = false;
        if (!myInline) ImGui::CloseCurrentPopup();
    }
}

void AppearanceEditor::DrawCreateDialog(AppContext& app, Session& session) {
    (void)session;
    ui::Ui& ui = app.ui;
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
    DrawCreateBody(app, width);
    ImGui::EndPopup();
}

void AppearanceEditor::DrawToolbarPicker(AppContext& app, Session* session, ImVec2 pos, float width, float height) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    // In an assembly it paints the component chosen there (nothing chosen:
    // nothing to paint), in a part every body.
    AssemblyState* st = session ? session->assembly.get() : nullptr;
    const ObjectId component = st && st->assembly.FindComponent(st->selected) ? st->selected : ObjectId{};
    const bool enabled = session && !session->model.Bodies().empty() && (!st || component);
    // What it looks like now: one appearance on every body, or a mix.
    const doc::Appearance* current = nullptr;
    bool mixed = false;
    if (enabled && st) {
        const doc::Look& look = st->ShownLook();
        current = look.OfBody(component);
        mixed = std::any_of(look.faces.begin(), look.faces.end(), [&](const doc::Look::FaceLook& f) { return f.body == component; });
    } else if (enabled) {
        const doc::Look& look = session->doc.look;
        const auto& bodies = session->model.Bodies();
        current = look.OfBody(bodies.front().id);
        for (const auto& b : bodies) mixed = mixed || look.OfBody(b.id) != current;
        mixed = mixed || !look.faces.empty();
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##look-picker-button", {width, height});
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, {pos.x + width, pos.y + height}, ui::ToU32(hovered ? c.hover : c.panel), ui.S(6));
    dl->AddRect(pos, {pos.x + width, pos.y + height}, ui::ToU32(c.fieldBorder), ui.S(6));
    const float s = ui.S(16);
    if (enabled) Sample(app, dl, {pos.x + ui.S(7), pos.y + (height - s) / 2}, s, mixed ? nullptr : current);
    const std::string name = !enabled ? std::string(st ? Tr("Выберите компонент") : Tr("Внешний вид")) : mixed ? std::string(Tr("Разные")) : current ? NameOf(*current) : std::string(Tr("По умолчанию"));
    const ImVec4 clip{pos.x, pos.y, pos.x + width - ui.S(22), pos.y + height};
    dl->AddText(ui.fonts.body, ui.S(12), {pos.x + ui.S(30), pos.y + (height - ui.S(12)) / 2 - ui.S(1)},
                ui::ToU32(enabled ? c.text : c.textMuted), name.c_str(), nullptr, 0, &clip);
    ui.icons.Draw(dl, "ui-chevron-down", {pos.x + width - ui.S(18), pos.y + (height - ui.S(10)) / 2}, ui.S(10), c.textMuted);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ui::ItemTooltip(ui, st ? Tr("Внешний вид выбранного компонента, только в этой сборке. Сначала выберите компонент. Грани и запись в деталь — «Вид → Внешний вид»")
                               : Tr("Внешний вид детали (всех тел). Грани и отдельные тела — «Вид → Внешний вид»"));
    if (clicked) {
        myLibrary.Reload();
        ImGui::OpenPopup("##look-picker");
    }
    ImGui::SetNextWindowPos({pos.x, pos.y + height + ui.S(4)});
    ImGui::SetNextWindowSizeConstraints({ui.S(240), 0}, {ui.S(260), ui.S(460)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(8, 8));
    const bool open = ImGui::BeginPopup("##look-picker");
    ImGui::PopStyleVar();
    if (!open) return;
    const std::string chosen = mixed || !current ? std::string() : current->id;
    if (const auto picked = List(app, mixed ? std::string_view("-") : std::string_view(chosen), ui.S(224), false); picked && session) {
        const doc::Appearance* a = picked->empty() ? nullptr : doc::FindStandard(*picked);
        if (!a && !picked->empty()) a = myLibrary.Find(*picked);
        if (st) {
            doc::Look look = st->assembly.look;
            look.SetBody(component, a);
            if (a && a->HasPicture()) look.pictures[a->picture] = myLibrary.PictureBytes(a->picture);
            if (look != st->assembly.look) {
                st->Checkpoint(Tr("Внешний вид"));
                st->assembly.look = std::move(look);
                st->assembly.TouchLook();
            }
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        doc::Look look = session->doc.look;
        for (const auto& b : session->model.Bodies()) look.SetBody(b.id, a);
        if (a && a->HasPicture()) look.pictures[a->picture] = myLibrary.PictureBytes(a->picture);
        if (look != session->doc.look) session->Execute(doc::SetLook(std::move(look), Tr("Внешний вид")));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

namespace {

// The size slider runs from 1 mm to 2 m, evenly in ratio (log scale): a
// small and a big picture are equally easy to set.
constexpr double kMinSize = 1.0, kMaxSize = 2000.0;
float SizeToSlider(double mm) {
    return static_cast<float>(std::log(std::clamp(mm, kMinSize, kMaxSize) / kMinSize) / std::log(kMaxSize / kMinSize));
}
double SliderToSize(float t) { return kMinSize * std::pow(kMaxSize / kMinSize, static_cast<double>(t)); }
/// Rounded to what a person would type: 3 significant digits.
double Tidy(double mm) {
    const double step = std::pow(10.0, std::floor(std::log10(mm)) - 2);
    return std::round(mm / step) * step;
}

} // namespace

void AppearanceEditor::DrawPictureSize(AppContext& app, Session& session, float width) {
    const doc::Appearance* chosen = Chosen();
    if (!chosen || !chosen->HasPicture()) return;
    ui::Ui& ui = app.ui;
    // The copy in the look being edited may be newer than the library's while the slider moves.
    doc::Look& look = LookOf(session);
    const doc::Appearance* inLook = look.Find(chosen->id);
    doc::Appearance a = inLook ? *inLook : *chosen;
    if (mySizeFor != a.id) {   // another appearance chosen: start from its size
        mySizeFor  = a.id;
        mySize     = a.pictureSize;
        mySizeText = std::format("{:g}", mySize);
    }
    a.pictureSize = mySize;
    ImGui::Dummy({0, ui.S(8)});
    Section(app, Tr("Размер текстуры: одна копия картинки, мм"));
    bool changed = false;
    float t = SizeToSlider(a.pictureSize);
    const float fieldW = ui.S(72);
    if (ui::Slider(ui, "##picture-size-slider", t, width - fieldW - ui.S(8))) {
        a.pictureSize = Tidy(SliderToSize(t));
        mySizeText = std::format("{:g}", a.pictureSize);
        changed = true;
    }
    bool finished = ImGui::IsItemDeactivated();
    ImGui::SameLine(0, ui.S(8));
    if (ui::TextField(ui, "##picture-size-field", mySizeText, fieldW)) {
        double value = 0;
        if (std::sscanf(mySizeText.c_str(), "%lf", &value) == 1 && value >= 0.01 && value <= 100000) {
            a.pictureSize = value;
            changed = true;
        }
    }
    finished = finished || ImGui::IsItemDeactivatedAfterEdit();
    Caption(app, Tr("Меньше — картинка мельче и повторяется сеткой чаще; крупнее — растягивается. "
                    "Разрешение файла не меняется: мелкая копия просто занимает меньше точек на экране."),
            width);
    mySize = a.pictureSize;
    if (changed && look.Refresh(a)) Touch(session);   // live on the part (or the assembly)
    // Kept in «Мои» when the slider is let go or the number typed in.
    if ((changed || finished) && a.id.starts_with("user.") && a.pictureSize != chosen->pictureSize && (finished || !ImGui::IsAnyItemActive())) {
        if (const auto saved = myLibrary.Save(a); !saved) app.notes.Error(Tr(saved.GetError().message));
    }
}

void AppearanceEditor::DrawPanel(AppContext& app, Session& session) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float width = ImGui::GetContentRegionAvail().x;

    // Header: icon, title, close (not on the phone: block 2's status line and ✕ under block 1).
    if (!app.shell.mobile.phone) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        ui.icons.Draw(dl, "appearance", {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        ui::FittedText(dl, ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, p.x + width - ui.S(40),
                       ui::ToU32(c.text), Title());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##close-look", "ui-close", 30, 14, Tr("Отмена (Esc)"))) app.Later([&app, &session, this] { Cancel(app, session); });
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }

    const bool phone = app.shell.mobile.phone;   // Отмена/Готово: the row under block 1
    const float footer = phone ? 0.0f : ui.S(62);
    ImGui::BeginChild("##look-body", {width, ImGui::GetContentRegionAvail().y - footer}, ImGuiChildFlags_None);
    ImGui::SetCursorPos({ui.S(14), ui.S(14)});
    ImGui::BeginGroup();
    const float inner = width - ui.S(28);

    // What is painted: a whole object (the default) or single faces.
    static constexpr std::array<ChoiceItem, 2> targets{{{"tree-part", N_("Тело целиком")}, {"work-plane", N_("Грань")}}};
    const int target = myFaces ? 1 : 0;
    if (const int t = ToolChoice(app, "look-target", N_("Что красить"), targets, target, inner); t != target) {
        myFaces = t == 1;
        myTargetsChosen = myChosen;   // the next choice goes on the targets in the new mode
    }
    if (phone) {
        if (session.assembly) {   // where it is kept: an island too
            static constexpr std::array<ChoiceItem, 2> places{{{nullptr, N_("Только в сборке")}, {nullptr, N_("В детали")}}};
            const int place = myToPart ? 1 : 0;
            if (const int p = ToolChoice(app, "look-place", N_("Где сохранить"), places, place, inner); p != place) myToPart = p == 1;
        }
        DrawPhonePanel(app, session, inner);
        if (TargetCount() > 0 && myChosen != myTargetsChosen) {
            myTargetsChosen = myChosen;
            ApplyToTargets(app, session);
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        return;
    }
    if (const std::size_t n = TargetCount(); n > 0)
        Caption(app, (myFaces ? Trf("Выбрано граней: {}. Вид из списка сразу ложится на них; щелчок по другой грани красит и её.", n)
                              : Trf("Выбрано объектов: {}. Вид из списка сразу ложится на них целиком; щелчок по другому объекту красит и его.", n))
                         .c_str(),
                inner);
    else
        Caption(app, myFaces ? Tr("Щелчок по грани красит только её; повторный щелчок другим видом перекрашивает.")
                             : session.assembly ? Tr("Щелчок по компоненту красит его целиком. Без выбранного компонента выбор в списке ничего не красит.")
                                                : Tr("Щелчок по телу красит его целиком (свои виды граней этого тела сбрасываются)."),
                inner);
    // Assembly: where the look is kept.
    if (session.assembly) {
        ImGui::Dummy({0, ui.S(8)});
        static constexpr std::array<ChoiceItem, 2> places{{{nullptr, N_("Только в сборке")}, {nullptr, N_("В детали")}}};
        const int place = myToPart ? 1 : 0;
        if (const int p = ToolChoice(app, "look-place", N_("Где сохранить"), places, place, inner); p != place) myToPart = p == 1;
        Caption(app, myToPart ? Tr("По «Готово» вид запишется в файлы деталей: изменятся все их копии, в этой и других сборках.")
                              : Tr("Вид только для этой сборки: файлы деталей не меняются."),
                inner);
    }
    ImGui::Dummy({0, ui.S(10)});

    // What a click puts on.
    {
        const doc::Appearance* a = Chosen();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + inner, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        Sample(app, dl, {p.x + ui.S(10), p.y + ui.S(10)}, h - ui.S(20), a);
        const std::string name = a ? NameOf(*a) : std::string(Tr("По умолчанию"));
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + h + ui.S(4), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), name.c_str());
        ImGui::Dummy({inner, h});
    }
    DrawPictureSize(app, session, inner);
    ImGui::Dummy({0, ui.S(10)});

    DrawLibrary(app, inner);
    if (TargetCount() > 0 && myChosen != myTargetsChosen) {
        myTargetsChosen = myChosen;
        ApplyToTargets(app, session);
    }

    if (!LookOf(session).Empty()) {
        ImGui::Dummy({0, ui.S(10)});
        if (ui::LinkButton(ui, "##reset-look", session.assembly ? Tr("Сбросить виды, заданные в сборке") : Tr("Сбросить все цвета детали"))) {
            LookOf(session) = {};
            Touch(session);
        }
    }
    ImGui::EndGroup();
    ImGui::EndChild();

    if (phone) {
        DrawCreateDialog(app, session);
        return;
    }
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
