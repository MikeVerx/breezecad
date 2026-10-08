#include "app/viewport/ViewportPanel.h"

#include "app/Operations.h"
#include "app/features/ExtrudeEditor.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
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

/// Half size of the origin plane squares: big enough to see around the part.
double PlaneHalfSize(const Session& session) {
    const auto box = session.model.BoundingBox();
    if (!box) return 40.0;
    const Vec3 d = box->max - box->min;
    return std::max({40.0, d.x * 0.65, d.y * 0.65, d.z * 0.65});
}

/// Ray from the cursor hits an origin plane square: returns the distance.
std::optional<double> HitPlane(const ViewportFrame& vf, ImVec2 mouse, const Frame& frame, double half) {
    Vec3 origin, dir;
    vf.projection.Unproject(vf.ToLocal(mouse), origin, dir);
    const double denom = dir.Dot(frame.normal);
    if (std::abs(denom) < 1e-9) return std::nullopt;
    const double t = (frame.origin - origin).Dot(frame.normal) / denom;
    const Vec2 local = frame.ToLocal(origin + dir * t);
    if (std::abs(local.x) > half || std::abs(local.y) > half) return std::nullopt;
    return t;
}

void DrawPlane(AppContext& app, ImDrawList* dl, const ViewportFrame& vf, ObjectId id, double half, bool hot) {
    const Frame frame = *doc::origin::PlaneFrame(id);
    const ui::ThemeColors& c = app.ui.C();
    const ImVec2 q[4] = {vf.Project(frame.ToWorld({-half, -half})), vf.Project(frame.ToWorld({half, -half})),
                         vf.Project(frame.ToWorld({half, half})), vf.Project(frame.ToWorld({-half, half}))};
    dl->AddConvexPolyFilled(q, 4, ui::ToU32(hot ? c.regionFill : c.planeFill));
    dl->AddPolyline(q, 4, ui::ToU32(hot ? c.sketchHover : c.planeEdge), ImDrawFlags_Closed, app.ui.S(hot ? 2.0f : 1.2f));
    const char* name = id == doc::origin::kPlaneXY ? "XY" : id == doc::origin::kPlaneXZ ? "XZ" : "YZ";
    const ImVec2 label = vf.Project(frame.ToWorld({-half * 0.92, half * 0.86}));
    dl->AddText(app.ui.fonts.strong, app.ui.S(12), label, ui::ToU32(hot ? c.sketchHover : c.planeEdge), name);
}

/// "Начать эскиз": choose an origin plane or a flat face.
void PickPlane(AppContext& app, Session& session, const ViewportFrame& vf, ImDrawList* dl) {
    const double half = PlaneHalfSize(session);
    const ImVec2 mouse = ImGui::GetMousePos();
    ObjectId hotPlane;
    double hotDistance = 1e300;
    if (vf.hovered)
        for (ObjectId id : {doc::origin::kPlaneXY, doc::origin::kPlaneXZ, doc::origin::kPlaneYZ})
            if (const auto t = HitPlane(vf, mouse, *doc::origin::PlaneFrame(id), half); t && *t < hotDistance) {
                hotDistance = *t;
                hotPlane = id;
            }
    // A model face in front of the planes wins.
    std::optional<render::PickResult> face;
    if (vf.hovered) face = app.viewport.Pick(vf.ToLocal(mouse));
    if (face && face->plane) {
        Vec3 origin, dir;
        vf.projection.Unproject(vf.ToLocal(mouse), origin, dir);
        if ((face->point - origin).Length() < hotDistance) hotPlane = {};
    }
    for (ObjectId id : {doc::origin::kPlaneXY, doc::origin::kPlaneXZ, doc::origin::kPlaneYZ})
        DrawPlane(app, dl, vf, id, half, id == hotPlane);

    // Prompt at the top of the viewport.
    const char* prompt = "Выберите плоскость или плоскую грань для эскиза";
    ImFont* font = app.ui.fonts.medium;
    const ImVec2 ts = font->CalcTextSizeA(app.ui.S(13), FLT_MAX, 0, prompt);
    const ImVec2 p{(vf.min.x + vf.max.x - ts.x) / 2 - app.ui.S(14), vf.min.y + app.ui.S(14)};
    dl->AddRectFilled(p, {p.x + ts.x + app.ui.S(28), p.y + app.ui.S(34)}, ui::ToU32(app.ui.C().panel.WithAlpha(0.94f)), app.ui.S(17));
    dl->AddText(font, app.ui.S(13), {p.x + app.ui.S(14), p.y + app.ui.S(9)}, ui::ToU32(app.ui.C().text), prompt);

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (hotPlane) {
        const ObjectId id = hotPlane;
        app.Later([&app, &session, id] { app.sketch.CreateAndEdit(app, session, {id, *doc::origin::PlaneFrame(id), false}); });
    } else if (face && face->plane) {
        const doc::SketchPlacement placement{face->body, *face->plane, true};
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
                    const doc::SketchPlacement placement{{}, *app.shell.nav.facePlane, true};
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
            if (!nav.moved && app.extrude.IsOpen()) app.Later([&app, &session] { app.extrude.Done(app, session); });
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

    // Pre-highlight faces under the cursor (not while sketching).
    const bool facesMatter = session.mode != EditMode::Sketch && !app.extrude.IsOpen();
    app.viewport.Hover(vf.hovered && facesMatter ? std::optional<Vec2>(vf.ToLocal(ImGui::GetMousePos())) : std::nullopt);

    // Mode overlays.
    switch (session.mode) {
        case EditMode::Sketch:
            app.sketch.Frame(app, session, vf);
            break;
        case EditMode::PickPlane:
            PickPlane(app, session, vf, dl);
            break;
        case EditMode::Model: {
            const double half = PlaneHalfSize(session);
            for (ObjectId id : {doc::origin::kPlaneXY, doc::origin::kPlaneXZ, doc::origin::kPlaneYZ})
                if (app.shell.hoveredTreeItem == id || session.selectedFeature == id || session.doc.FeatureCount() == 0)
                    DrawPlane(app, dl, vf, id, half, app.shell.hoveredTreeItem == id || session.selectedFeature == id);
            // Sketches not used by an operation stay visible, like in Inventor.
            for (std::size_t i = 0; i < session.doc.EndMarker(); ++i) {
                const auto* sk = dynamic_cast<const doc::SketchFeature*>(session.doc.Features()[i].get());
                if (!sk || sk->suppressed) continue;
                bool used = false;
                for (const auto& f : session.doc.Features())
                    if (const auto* ex = dynamic_cast<const doc::ExtrudeFeature*>(f.get()); ex && ex->sketch == sk->id) used = true;
                if (used && !(app.extrude.IsOpen())) continue;
                SketchDrawStyle style;
                style.passive = true;
                DrawSketch(ui, dl, *sk, {sk->placement.frame, vf}, session.doc.units, style);
            }
            if (app.extrude.IsOpen()) app.extrude.DrawInViewport(app, session, vf);

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
            if (vf.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                if (const auto hit = app.viewport.Pick(vf.ToLocal(ImGui::GetMousePos())); hit && hit->body) {
                    const ObjectId body = hit->body;
                    app.Later([&app, &session, body] { EditFeature(app, session, body); });
                }
            break;
        }
    }
    ContextMenu(app, session);

    // View cube and triad on top of everything.
    const ui::ViewCubeAction cube = ui::DrawViewCube(ui, vf.projection, pos, {pos.x + size.x, pos.y + size.y});
    if (cube.lookFrom) LookFrom(app, cube.lookFrom->first, cube.lookFrom->second);
    if (cube.home) HomeView(app);
    if (cube.dragYaw != 0 || cube.dragPitch != 0) app.viewport.Orbit(cube.dragYaw, cube.dragPitch, app.viewport.ModelCenter());

    session.camera = app.viewport.Camera();
    EndArea();
}

} // namespace bz::app
