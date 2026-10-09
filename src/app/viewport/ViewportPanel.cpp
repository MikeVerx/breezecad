#include "app/viewport/ViewportPanel.h"

#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "app/viewport/Measure.h"
#include "app/viewport/Planes.h"
#include "app/viewport/Section.h"
#include "app/shell/Shell.h"
#include "document/ExtrudeFeature.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
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
    const ShownPlane* hot = vf.hovered ? PlaneUnderCursor(vf, mouse, planes) : nullptr;
    // A flat face of the model under the cursor wins over the planes, even
    // where a plane sticks out in front of it: pointing at the body means
    // the body (as in Inventor).
    std::optional<render::PickResult> face;
    if (vf.hovered) face = app.viewport.Pick(vf.ToLocal(mouse));
    if (face && face->plane) hot = nullptr;
    // With a body in the part the planes step back, so the body stays readable.
    const bool faint = !session.model.Bodies().empty();
    for (const ShownPlane& p : planes) DrawPlane(app, dl, vf, p, &p == hot, faint && &p != hot);

    // Prompt at the top of the viewport.
    const char* prompt = "Выберите плоскость или плоскую грань для эскиза";
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.S(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui.S(8, 6));
    ui::FontScope font(ui.fonts.body);   // before Begin: must outlive the End call
    if (ImGui::BeginPopup("##viewport-menu")) {
        const auto item = [&](const char* id, const char* label = nullptr) {
            const Action* a = app.actions.Find(id);
            const std::string sc = ActionRegistry::ChordText(a->chord);
            if (ImGui::MenuItem(label ? label : a->label.c_str(), sc.empty() ? nullptr : sc.c_str(), false, a->IsEnabled())) {
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
            item("edit.delete");
        } else {
            if (app.shell.nav.facePlane) {
                if (ImGui::MenuItem("Эскиз на этой грани")) {
                    const doc::SketchPlacement placement{app.shell.nav.faceBody, *app.shell.nav.facePlane, true,
                                                         {app.shell.nav.facePlane->normal, app.shell.nav.pivot}};
                    app.Later([&app, &session, placement] { app.sketch.CreateAndEdit(app, session, placement); });
                }
            }
            item("sketch.start");
            item("feature.extrude");
            ImGui::Separator();
            item("view.home");
            item("view.fit");
            if (ImGui::BeginMenu("Стиль отображения")) {
                for (const char* id : {"view.shaded", "view.edges", "view.wireframe", "view.hidden", "view.transparent"}) item(id);
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

/// Mouse navigation, KOMPAS style. Returns true while navigating.
bool Navigate(AppContext& app, Session& session, const ViewportFrame& vf, bool sceneHovered) {
    ShellState::Navigation& nav = app.shell.nav;
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();

    if (sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        nav.orbiting = true;
        nav.moved = false;
        // Orbit around the model point under the cursor, else the model centre.
        const auto hit = app.viewport.Pick(vf.ToLocal(mouse));
        nav.pivot = hit ? hit->point : app.viewport.ModelCenter();
        nav.facePlane = hit ? hit->plane : std::nullopt;
        nav.faceBody = hit ? hit->body : ObjectId{};
    }
    if (sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        nav.panning = true;
        nav.moved = false;
    }

    if (nav.orbiting) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 3.0f)) nav.moved = true;
        if (nav.moved && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0))
            app.viewport.Orbit(io.MouseDelta.x * kOrbitPerPixel, io.MouseDelta.y * kOrbitPerPixel, nav.pivot);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            nav.orbiting = false;
            if (!nav.moved) ImGui::OpenPopup("##viewport-menu");   // a short right click: context menu
        }
    }
    if (nav.panning) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 3.0f)) nav.moved = true;
        if (nav.moved && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) app.viewport.Pan(io.MouseDelta.x, io.MouseDelta.y);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            nav.panning = false;
            // A middle click confirms the operation, like in KOMPAS.
            if (!nav.moved && app.ops.Active()) app.Later([&app, &session] { EnterPressed(app); });
        }
    }
    if (sceneHovered && io.MouseWheel != 0.0f) app.viewport.ZoomAt(std::pow(1.15, static_cast<double>(io.MouseWheel)), vf.ToLocal(mouse));
    return (nav.orbiting || nav.panning) && nav.moved;
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

    app.shell.planeQuads.clear();   // refilled by the overlays below

    // Pre-highlight faces under the cursor (not while sketching).
    OperationEditor* operation = app.ops.Active();
    const bool pickingOriginal = operation && operation->PicksOriginal();
    if (!pickingOriginal) app.viewport.EndPickingOriginal();
    // Measuring makes vertices and edges pickable too.
    const bool measuring = app.shell.measuring && session.mode == EditMode::Model && !operation;
    app.viewport.SetMeasuring(measuring);
    // Pre-highlight the face (or, for fillets, the edge) under the cursor.
    const bool hoverMatters = session.mode != EditMode::Sketch && (!operation || pickingOriginal || operation->PicksFaces());
    app.viewport.Hover(vf.hovered && hoverMatters ? std::optional<Vec2>(vf.ToLocal(ImGui::GetMousePos())) : std::nullopt);

    // Mode overlays.
    switch (session.mode) {
        case EditMode::Sketch:
            app.sketch.Frame(app, session, vf);
            break;
        case EditMode::PickPlane:
            PickPlane(app, session, vf, dl);
            break;
        case EditMode::Model: {
            // Origin planes only when pointed at in the tree (or in an empty
            // part); work planes always, faint unless pointed at.
            const auto pointedAt = [&](ObjectId id) { return app.shell.hoveredTreeItem == id || session.selectedFeature == id; };
            if (!operation || !operation->DrawsPlanes()) {
                for (const ShownPlane& p : OriginPlanes(session))
                    if (pointedAt(p.id) || session.doc.FeatureCount() == 0) DrawPlane(app, dl, vf, p, pointedAt(p.id));
                for (const ShownPlane& p : WorkPlanes(session)) DrawPlane(app, dl, vf, p, pointedAt(p.id), !pointedAt(p.id));
            }
            // Sketches not used by an operation stay visible, like in Inventor.
            // (An open sketch operation draws its own sketch.)
            for (std::size_t i = 0; i < session.doc.EndMarker(); ++i) {
                const auto* sk = dynamic_cast<const doc::SketchFeature*>(session.doc.Features()[i].get());
                if (!sk || sk->suppressed) continue;
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
                const char* text = "Начните с эскиза: «Начать эскиз» на ленте (S), затем выберите плоскость";
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
    if (modelMode) DrawSectionBar(app, session, vf);
    if (measuring) DrawMeasure(app, session, vf);
    ContextMenu(app, session);
    app.viewport.SetPlanes(app.shell.planeQuads);

    // View cube and triad on top of everything.
    const ui::ViewCubeAction cube = ui::DrawViewCube(ui, vf.projection, pos, {pos.x + size.x, pos.y + size.y});
    if (cube.lookFrom) LookFrom(app, cube.lookFrom->first, cube.lookFrom->second);
    if (cube.home) HomeView(app);
    if (cube.dragYaw != 0 || cube.dragPitch != 0) app.viewport.Orbit(cube.dragYaw, cube.dragPitch, app.viewport.ModelCenter());

    session.camera = app.viewport.Camera();
    EndArea();
}

} // namespace bz::app
