#include "app/viewport/FaceSelection.h"

#include "app/Session.h"
#include "app/features/OperationEditors.h"
#include "core/I18n.h"
#include "core/Monitor.h"
#include "ui/Menus.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace bz::app {
namespace {

constexpr float kFramePixels = 5.0f;   // a press moved further than this draws a frame

using Faces = std::vector<render::SelectedFace>;

bool SelectingFaces(AppContext& app, const Session& session) {
    return session.mode == EditMode::Model && !session.IsAssembly() && app.ops.Active() == nullptr && !app.shell.measuring;
}

/// Both point at the same face of the same body (two clicks on one face
/// give two references with different points).
bool Same(AppContext& app, const render::SelectedFace& a, const render::SelectedFace& b) {
    return a.body == b.body && (a.face == b.face || app.viewport.SameFace(a.body, a.face, b.face));
}

bool Contains(AppContext& app, const Faces& faces, const render::SelectedFace& f) {
    return std::any_of(faces.begin(), faces.end(), [&](const render::SelectedFace& g) { return Same(app, g, f); });
}

void Add(AppContext& app, Faces& faces, const Faces& more) {
    for (const render::SelectedFace& f : more)
        if (!Contains(app, faces, f)) faces.push_back(f);
}

void Remove(AppContext& app, Faces& faces, const Faces& less) {
    std::erase_if(faces, [&](const render::SelectedFace& f) { return Contains(app, less, f); });
}

/// The face under the cursor, if any.
std::optional<render::SelectedFace> FaceAt(AppContext& app, const ViewportFrame& vf, ImVec2 mouse) {
    const auto hit = app.viewport.Pick(vf.ToLocal(mouse));
    if (!hit || !hit->body || !hit->face) return std::nullopt;
    return render::SelectedFace{hit->body, *hit->face};
}

} // namespace

void UpdateFaceSelection(AppContext& app, Session& session, const ViewportFrame& vf) {
    ShellState::FaceSelecting& fs = app.shell.faceSelecting;
    // A sketch or another plane being chosen: the faces chosen before are let go.
    if (session.mode != EditMode::Model) session.selectedFaces.clear();
    const bool active = SelectingFaces(app, session);
    app.viewport.SetSelectedFaces(active ? session.selectedFaces : Faces{});
    if (!active) {
        fs = {};
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();

    // The left button: a click or a frame. (A double click edits the
    // operation that made the body; it changes nothing here.)
    if (vf.hovered && !app.shell.handleHot && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        fs.pressed = true;
        fs.framing = false;
        fs.from = mouse;
    }
    if (fs.pressed && !fs.framing && ImGui::IsMouseDragging(ImGuiMouseButton_Left, kFramePixels)) fs.framing = true;
    if (fs.framing) {
        // As in the sketch: a window from left to right, a crossing frame back.
        const bool window = mouse.x >= fs.from.x;
        const ui::ThemeColors& c = app.ui.C();
        const Color tone = window ? c.danger : c.success;
        const ImVec2 lo{std::min(fs.from.x, mouse.x), std::min(fs.from.y, mouse.y)};
        const ImVec2 hi{std::max(fs.from.x, mouse.x), std::max(fs.from.y, mouse.y)};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(vf.min, vf.max, true);
        dl->AddRectFilled(lo, hi, ui::ToU32(tone.WithAlpha(0.10f)));
        dl->AddRect(lo, hi, ui::ToU32(tone), 0, 0, app.ui.S(1.2f));
        dl->PopClipRect();
    }
    if (fs.pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        Faces& chosen = session.selectedFaces;
        if (fs.framing) {
            const bool crossing = mouse.x < fs.from.x;
            const Faces found = app.viewport.FacesInRect(vf.ToLocal(fs.from), vf.ToLocal(mouse), crossing);
            if (io.KeyCtrl) Remove(app, chosen, found);
            else if (io.KeyShift) Add(app, chosen, found);
            else chosen = found;
            mon::Action("select", crossing ? "рамка (касание)" : "рамка");
        } else if (const auto face = FaceAt(app, vf, mouse)) {
            if (io.KeyShift || io.KeyCtrl) {
                if (Contains(app, chosen, *face)) Remove(app, chosen, {*face});
                else chosen.push_back(*face);
            } else {
                chosen = {*face};
            }
            mon::Action("select", "грань");
        } else if (!io.KeyShift && !io.KeyCtrl) {
            chosen.clear();   // a click on an empty spot
        }
        fs = {};
        app.viewport.SetSelectedFaces(chosen);
    }

    // A short right click on a face not chosen: it becomes the choice, so
    // the context menu that opens now acts on it (as in Inventor).
    if (vf.hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0 &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).y == 0)
        if (const auto face = FaceAt(app, vf, mouse); face && !Contains(app, session.selectedFaces, *face)) {
            session.selectedFaces = {*face};
            app.viewport.SetSelectedFaces(session.selectedFaces);
        }
}

void FaceSelectionMenu(AppContext& app, Session& session) {
    if (session.selectedFaces.empty() || !SelectingFaces(app, session)) return;
    ui::Ui& ui = app.ui;
    const Faces faces = session.selectedFaces;
    const std::string count = Trf("Выбрано граней: {}", faces.size());
    ui::MenuItem(ui, count.c_str(), nullptr, {}, false, false);
    if (ui::MenuItem(ui, Tr("Внешний вид или текстура…"), nullptr, "appearance")) {
        mon::Action("select", "внешний вид граней");
        app.Later([&app, &session, faces] { app.ops.appearance.StartOnFaces(app, session, faces); });
    }
    // Faces with a look of their own: it can go (the body's look shows again).
    bool ownLook = false;
    for (const doc::Look::FaceLook& f : session.doc.look.faces)
        for (const render::SelectedFace& s : faces)
            ownLook = ownLook || (f.body == s.body && app.viewport.SameFace(s.body, f.face, s.face));
    if (ui::MenuItem(ui, Tr("Сбросить внешний вид граней"), nullptr, {}, false, ownLook))
        app.Later([&app, &session, faces] { AppearanceEditor::ResetFaces(app, session, faces); });
    if (ui::MenuItem(ui, Tr("Снять выделение"), "Esc")) session.selectedFaces.clear();
    ImGui::Separator();
}

} // namespace bz::app
