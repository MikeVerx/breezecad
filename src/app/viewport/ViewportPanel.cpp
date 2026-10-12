#include "app/viewport/ViewportPanel.h"
#include "core/Monitor.h"

#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "app/assembly/AssemblyUi.h"
#include "app/viewport/MarkingMenu.h"
#include "app/viewport/Measure.h"
#include "app/viewport/Planes.h"
#include "app/viewport/Section.h"
#include "core/I18n.h"
#include "document/ExtrudeFeature.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
#include "ui/Menus.h"
#include "ui/ViewCube.h"
#include "ui/Widgets.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace bz::app {
namespace {

constexpr double kOrbitPerPixel = 0.0075;   // radians, KOMPAS-like sensitivity

/// "Начать эскиз": choose an origin plane, a work plane or a flat face.
void PickPlane(AppContext& app, Session& session, const ViewportFrame& vf, ImDrawList* dl) {
    const ImVec2 mouse = ImGui::GetMousePos();
    std::vector<ShownPlane> planes = OriginPlanes(session);
    const std::vector<ShownPlane> work = WorkPlanes(session);
    planes.insert(planes.end(), work.begin(), work.end());

    // A plane chosen in the tree meanwhile (a hidden one too): the sketch goes on it.
    std::optional<Frame> chosen = doc::origin::PlaneFrame(session.selectedFeature);
    if (const auto* wp = session.doc.FindAs<doc::WorkPlaneFeature>(session.selectedFeature); wp && wp->frame && !wp->suppressed)
        chosen = wp->frame;
    if (chosen) {
        const doc::SketchPlacement placement{session.selectedFeature, *chosen, false, {}};
        app.Later([&app, &session, placement] { app.sketch.CreateAndEdit(app, session, placement); });
        return;
    }

    const ShownPlane* hot = vf.hovered ? PlaneUnderCursor(vf, mouse, planes) : nullptr;
    // A flat face of the model under the cursor wins over the planes, even
    // where a plane sticks out in front of it: pointing at the body means
    // the body (as in Inventor).
    std::optional<render::PickResult> face;
    if (vf.hovered) face = app.viewport.Pick(vf.ToLocal(mouse));
    // A work plane the user made stands in front of that face, though,
    // wins: it overlaps the body, so otherwise it could hardly be clicked.
    if (face && face->plane && hot) {
        const bool workPlane = !doc::origin::IsOriginElement(hot->id);
        Vec3 eye, dir;
        vf.projection.Unproject(vf.ToLocal(mouse), eye, dir);
        const std::optional<double> planeAt = HitPlane(vf, mouse, *hot);
        const double faceAt = (face->point - eye).Dot(dir) / dir.Dot(dir);
        if (!workPlane || !planeAt || *planeAt > faceAt - 1e-6) hot = nullptr;
    } else if (face && face->plane) {
        hot = nullptr;
    }
    // With a body in the part the planes step back, so the body stays readable.
    const bool faint = !session.model.Bodies().empty();
    for (const ShownPlane& p : planes) DrawPlane(app, dl, vf, p, &p == hot, faint && &p != hot);

    // Prompt at the top of the viewport.
    const char* prompt = Tr("Выберите плоскость или плоскую грань для эскиза");
    ImFont* font = app.ui.fonts.medium;
    const ImVec2 ts = font->CalcTextSizeA(app.ui.S(13), FLT_MAX, 0, prompt);
    const ImVec2 p{(vf.min.x + vf.max.x - ts.x) / 2 - app.ui.S(14), vf.min.y + app.ui.S(14)};
    dl->AddRectFilled(p, {p.x + ts.x + app.ui.S(28), p.y + app.ui.S(34)}, ui::ToU32(app.ui.C().panel.WithAlpha(0.94f)), app.ui.S(17));
    dl->AddText(font, app.ui.S(13), {p.x + app.ui.S(14), p.y + app.ui.S(9)}, ui::ToU32(app.ui.C().text), prompt);

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (hot) {
        const doc::SketchPlacement placement{hot->id, hot->frame, false, {}};
        app.Later([&app, &session, placement] { app.sketch.CreateAndEdit(app, session, placement); });
    } else if (face && face->plane) {
        const doc::SketchPlacement placement{face->body, *face->plane, true, {face->plane->normal, face->point}};
        app.Later([&app, &session, placement] { app.sketch.CreateAndEdit(app, session, placement); });
    }
}

void ContextMenu(AppContext& app, Session& session) {
    ui::Ui& ui = app.ui;
    ui::MenuStyle style(ui);   // before Begin: must outlive the End call
    if (!ImGui::BeginPopup("##viewport-menu")) return;
    const auto item = [&](const char* id) {
        const Action* a = app.actions.Find(id);
        const std::string sc = ActionRegistry::ChordText(a->chord);
        if (ui::MenuItem(ui, a->Label(), sc.empty() ? nullptr : sc.c_str(), a->icon, false, a->IsEnabled())) {
            const std::string action = id;
            app.Later([&app, action] { app.actions.Run(action); });
        }
    };
    if (session.mode == EditMode::Sketch) {
        item("sketch.finish");
        item("sketch.lookAt");
        ImGui::Separator();
        item("sketch.line");
        item("sketch.rectangle");
        item("sketch.circle");
        item("sketch.dimension");
        ImGui::Separator();
        item("sketch.construction");
        item("sketch.centerline");
        item("edit.delete");
    } else {
        if (app.shell.nav.facePlane && ui::MenuItem(ui, Tr("Эскиз на этой грани"), nullptr, "sketch")) {
            const doc::SketchPlacement placement{app.shell.nav.faceBody, *app.shell.nav.facePlane, true,
                                                 {app.shell.nav.facePlane->normal, app.shell.nav.pivot}};
            app.Later([&app, &session, placement] { app.sketch.CreateAndEdit(app, session, placement); });
        }
        item("sketch.start");
        item("feature.extrude");
        ImGui::Separator();
        item("tools.measure");
        item("view.section");
        ImGui::Separator();
        item("view.home");
        item("view.fit");
        if (ui::BeginSubmenu(ui, Tr("Стиль отображения"), "ui-display-style")) {
            for (const char* id : {"view.shaded", "view.edges", "view.wireframe", "view.hidden", "view.transparent"}) item(id);
            ImGui::EndMenu();
        }
    }
    ImGui::EndPopup();
}

/// Whether the modifier keys held now are exactly `m`.
bool ModifierHeld(ModifierChoice m) {
    const ImGuiIO& io = ImGui::GetIO();
    switch (m) {
        case ModifierChoice::None:  return !io.KeyShift && !io.KeyCtrl && !io.KeyAlt;
        case ModifierChoice::Shift: return io.KeyShift && !io.KeyCtrl && !io.KeyAlt;
        case ModifierChoice::Ctrl:  return io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
        case ModifierChoice::Alt:   return io.KeyAlt && !io.KeyShift && !io.KeyCtrl;
    }
    return false;
}

int ImGuiButton(MouseButtonChoice b) {
    return b == MouseButtonChoice::Right ? ImGuiMouseButton_Right : b == MouseButtonChoice::Middle ? ImGuiMouseButton_Middle : -1;
}

/// Mouse navigation with the gestures from the settings (orbit, pan, drag
/// zoom; the wheel always zooms). Returns true while navigating.
bool Navigate(AppContext& app, Session& session, const ViewportFrame& vf, bool sceneHovered) {
    ShellState::Navigation& nav = app.shell.nav;
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();
    const MouseNavigation& m = app.settings.mouse;
    const bool busy = nav.orbiting || nav.panning || nav.zooming;

    // A press starts the gesture bound to that button and the modifiers held.
    for (int button : {ImGuiMouseButton_Right, ImGuiMouseButton_Middle}) {
        if (busy || !sceneHovered || !ImGui::IsMouseClicked(button)) continue;
        const auto matches = [&](const MouseGesture& g) { return ImGuiButton(g.button) == button && ModifierHeld(g.modifier); };
        nav.moved = false;
        nav.button = button;
        if (matches(m.orbit)) {
            nav.orbiting = true;
            // Orbit around the model point under the cursor, else the model centre.
            const auto hit = app.viewport.Pick(vf.ToLocal(mouse));
            nav.pivot = hit ? hit->point : app.viewport.ModelCenter();
            nav.facePlane = hit ? hit->plane : std::nullopt;
            nav.faceBody = hit ? hit->body : ObjectId{};
        } else if (matches(m.pan)) {
            nav.panning = true;
        } else if (matches(m.zoom)) {
            nav.zooming = true;
            nav.zoomAt = mouse;
        } else if (button == ImGuiMouseButton_Right) {
            nav.button = -1;   // a right click not bound to a gesture: just the menu, on release
        }
    }
    // In a sketch, a right click that ends a chain is not a menu click.
    const bool sketchBusy = session.mode == EditMode::Sketch && app.sketch.Busy();
    // The radial menu took this right press: its release opens nothing else.
    if (nav.button == -2) {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) nav.button = -1;
        return false;
    }
    if (nav.button < 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && sceneHovered && !busy && !sketchBusy &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0 && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).y == 0)
        ImGui::OpenPopup("##viewport-menu");

    if (nav.orbiting || nav.panning || nav.zooming) {
        if (!nav.moved && ImGui::IsMouseDragging(nav.button, 3.0f)) {
            nav.moved = true;
            mon::Action("view", nav.orbiting ? "вращение" : nav.panning ? "сдвиг" : "масштаб");
        }
        if (nav.moved && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
            if (nav.orbiting) {
                const double k = kOrbitPerPixel * app.settings.orbitSpeed;
                app.viewport.Orbit(io.MouseDelta.x * k, io.MouseDelta.y * k * (app.settings.invertOrbit ? -1.0 : 1.0), nav.pivot);
            }
            if (nav.panning) app.viewport.Pan(io.MouseDelta.x, io.MouseDelta.y);
            if (nav.zooming) app.viewport.ZoomAt(std::pow(1.01, static_cast<double>(-io.MouseDelta.y)), vf.ToLocal(nav.zoomAt));
        }
        if (ImGui::IsMouseReleased(nav.button)) {
            // A short right click: the context menu; a short middle click
            // confirms the operation, like in KOMPAS.
            if (!nav.moved && nav.button == ImGuiMouseButton_Right && !sketchBusy) ImGui::OpenPopup("##viewport-menu");
            if (!nav.moved && nav.button == ImGuiMouseButton_Middle && app.ops.Active())
                app.Later([&app, &session] { EnterPressed(app); });
            nav.orbiting = nav.panning = nav.zooming = false;
            nav.button = -1;
        }
    }
    if (sceneHovered && io.MouseWheel != 0.0f) {
        static double lastWheel = -10.0;   // one action per burst of wheel turns
        if (app.now - lastWheel > 1.0) mon::Action("view", "колесо");
        lastWheel = app.now;
        const double steps = static_cast<double>(m.invertWheel ? -io.MouseWheel : io.MouseWheel);
        app.viewport.ZoomAt(std::pow(1.15, steps), vf.ToLocal(mouse));
    }
    return (nav.orbiting || nav.panning || nav.zooming) && nav.moved;
}

} // namespace

void DrawViewport(AppContext& app, Session& session, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    ImDrawList* dl = BeginArea("##viewport", pos, size, ui.C().viewportBottom);
    app.shell.viewportMax = {pos.x + size.x, pos.y + size.y};

    // Size the 3D view and give each document its own camera.
    int winW = 1, fbW = 1, h = 1;
    glfwGetWindowSize(glfwGetCurrentContext(), &winW, &h);
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fbW, &h);
    app.viewport.SetSize(size.x, size.y, winW > 0 ? static_cast<double>(fbW) / winW : 1.0);
    if (app.shell.viewportSession != &session) {
        app.shell.viewportSession = &session;
        if (!session.hasCamera) {
            session.camera = app.viewport.FitCamera(-ui::kHomeDirection, {0, 0, 1});
            session.hasCamera = true;
        }
        app.viewport.SetCamera(session.camera);
    }

    if (const unsigned texture = app.viewport.TextureId())
        dl->AddImage(static_cast<ImTextureID>(texture), pos, {pos.x + size.x, pos.y + size.y}, {0, 1}, {1, 0});

    // The scene surface takes all mouse buttons; overlays drawn later may overlap it.
    ImGui::SetCursorScreenPos(pos);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##scene", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    ViewportFrame vf{pos, {pos.x + size.x, pos.y + size.y}, app.viewport.Projection(), false};
    const bool navigating = Navigate(app, session, vf, ImGui::IsItemHovered());
    vf.projection = app.viewport.Projection();
    vf.hovered = ImGui::IsItemHovered() && !navigating;
    if (navigating && app.shell.nav.orbiting) {
        // The point the view turns around: under the cursor when the turn
        // began (or the model centre), so it is clear what stays in place.
        const ui::ThemeColors& c = ui.C();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 at = vf.Project(app.shell.nav.pivot);
        const float r = ui.S(5);
        dl->AddCircleFilled(at, r + ui.S(2), ui::ToU32(c.panel.WithAlpha(0.9f)));
        dl->AddCircleFilled(at, r, ui::ToU32(c.accent));
        dl->AddCircle(at, r + ui.S(7), ui::ToU32(c.accent.WithAlpha(0.45f)), 0, ui.S(1.2f));
    }
    UpdateMarkingMenu(app, session, vf);   // may take the mouse from the overlays
    app.shell.handleHot = false;           // a handle drawn below sets it again for the next frame

    app.shell.planeQuads.clear();   // refilled by the overlays below

    // Pre-highlight faces under the cursor (not while sketching).
    OperationEditor* operation = app.ops.Active();
    // Model edges are picked on the bodies before an operation (fillet,
    // shell) or before the sketch that projects them.
    const bool projecting = session.mode == EditMode::Sketch && app.sketch.Tool() == SketchTool::Project;
    const bool pickingOriginal = (operation && operation->PicksOriginal()) || projecting;
    if (projecting) {
        if (const int index = session.doc.IndexOf(session.activeSketch); index >= 0) {
            app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::EdgesAndFaces);
            app.viewport.SetMarkedEdges({});
        }
    } else {
        app.viewport.ExpirePickingOriginal();
    }
    // Measuring makes vertices and edges pickable too.
    const bool measuring = app.shell.measuring && session.mode == EditMode::Model && !operation;
    app.viewport.SetMeasuring(measuring);
    // Pre-highlight the face (or, for fillets, the edge) under the cursor.
    const bool hoverMatters =
        projecting || (session.mode != EditMode::Sketch && (!operation || pickingOriginal || operation->PicksFaces()));
    {
        const double start = glfwGetTime();
        app.viewport.Hover(vf.hovered && hoverMatters ? std::optional<Vec2>(vf.ToLocal(ImGui::GetMousePos())) : std::nullopt);
        app.stats.hoverMs = app.stats.hoverMs * 0.9 + (glfwGetTime() - start) * 100.0;   // ms, smoothed
    }

    // Mode overlays.
    switch (session.mode) {
        case EditMode::Sketch:
            app.sketch.Frame(app, session, vf);
            break;
        case EditMode::PickPlane:
            PickPlane(app, session, vf, dl);
            break;
        case EditMode::Model: {
            if (session.assembly) {
                DrawAssemblyViewport(app, session, vf);
                break;
            }
            // Origin planes only when pointed at in the tree (or in an empty
            // part); work planes always, faint unless pointed at.
            const auto pointedAt = [&](ObjectId id) { return app.shell.hoveredTreeItem == id || session.selectedFeature == id; };
            if (!operation || !operation->DrawsPlanes()) {
                for (const ShownPlane& p : OriginPlanes(session))
                    if (pointedAt(p.id) || session.doc.FeatureCount() == 0) DrawPlane(app, dl, vf, p, pointedAt(p.id));
                for (const ShownPlane& p : WorkPlanes(session)) DrawPlane(app, dl, vf, p, pointedAt(p.id), !pointedAt(p.id));
            }
            DrawWorkAxes(app, dl, vf, session, static_cast<std::size_t>(-1),
                         app.shell.hoveredTreeItem ? app.shell.hoveredTreeItem : session.selectedFeature);
            if (!operation || !operation->DrawsPoints())
                DrawWorkPoints(app, dl, vf, WorkPoints(session),
                               app.shell.hoveredTreeItem ? app.shell.hoveredTreeItem : session.selectedFeature);
            // Sketches not used by an operation stay visible, like in Inventor.
            // (An open sketch operation draws its own sketch.)
            for (std::size_t i = 0; i < session.doc.EndMarker(); ++i) {
                const auto* sk = dynamic_cast<const doc::SketchFeature*>(session.doc.Features()[i].get());
                if (!sk || sk->suppressed || sk->hidden) continue;
                bool used = false;
                for (const auto& f : session.doc.Features())
                    for (ObjectId dep : f->Dependencies()) used = used || dep == sk->id;
                if (used) continue;
                SketchDrawStyle style;
                style.passive = true;
                DrawSketch(ui, dl, *sk, {sk->placement.frame, vf}, session.doc.units, style);
            }
            if (operation) operation->DrawInViewport(app, session, vf);

            // A friendly start for an empty part.
            if (session.doc.FeatureCount() == 0) {
                const char* text = Tr("Начните с эскиза: «Начать эскиз» на ленте (S), затем выберите плоскость");
                ImFont* font = ui.fonts.medium;
                const ImVec2 ts = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, text);
                const ImVec2 p{pos.x + (size.x - ts.x) / 2 - ui.S(14), pos.y + ui.S(14)};
                dl->AddRectFilled(p, {p.x + ts.x + ui.S(28), p.y + ui.S(34)}, ui::ToU32(ui.C().panel.WithAlpha(0.94f)), ui.S(17));
                dl->AddText(font, ui.S(13), {p.x + ui.S(14), p.y + ui.S(9)}, ui::ToU32(ui.C().textSecondary), text);
            }
            // Double click on the model: edit the operation that made the body.
            if (!measuring && vf.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                if (const auto hit = app.viewport.Pick(vf.ToLocal(ImGui::GetMousePos())); hit && hit->body) {
                    const ObjectId body = hit->body;
                    app.Later([&app, &session, body] { EditFeature(app, session, body); });
                }
            break;
        }
    }
    // The section bar sits over the model, under the context menu.
    const bool modelMode = session.mode == EditMode::Model;
    app.viewport.SetSection(modelMode ? SectionFrame(session, app.shell.section) : std::nullopt);
    app.viewport.ShowInterferences(modelMode && app.shell.interference ? &session.model : nullptr);
    // A pattern or mirror being edited: its sources and copies outlined.
    {
        const OperationEditor* op = app.ops.Active();
        const bool copying = op && (op == &app.ops.pattern || op == &app.ops.mirror);
        app.viewport.ShowCopyPreview(copying ? &session.model : nullptr, copying ? op->EditedFeature() : ObjectId{});
    }
    if (modelMode) DrawSectionBar(app, session, vf);
    if (measuring) DrawMeasure(app, session, vf);
    ContextMenu(app, session);
    DrawMarkingMenu(app, session);
    app.viewport.SetPlanes(app.shell.planeQuads);

    // View cube and triad on top of everything.
    const ui::ViewCubeAction cube = ui::DrawViewCube(ui, vf.projection, pos, {pos.x + size.x, pos.y + size.y}, app.settings.showTriad);
    if (cube.lookFrom) LookFrom(app, cube.lookFrom->first, cube.lookFrom->second);
    if (cube.home) HomeView(app);
    if (cube.dragYaw != 0 || cube.dragPitch != 0) app.viewport.Orbit(cube.dragYaw, cube.dragPitch, app.viewport.ModelCenter());

    session.camera = app.viewport.Camera();
    EndArea();
}

} // namespace bz::app
