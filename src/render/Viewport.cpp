#include "render/Viewport.h"

#include "core/Log.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/Measure.h"
#include "kernel/occt/ModelImpl.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <gp_Cylinder.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_Shape.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Graphic3d_Mat4d.hxx>
#include <Graphic3d_Vec2.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopExp.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_Texture.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_IsoAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopoDS.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <GLFW/glfw3.h>   // glfwGetTime for camera animation

#include <algorithm>
#include <type_traits>
#include <cmath>
#include <vector>

namespace bz::render {
namespace {

/// Lights that follow the camera: a key light from the upper left and a
/// weaker fill from the right, plus ambient. A single light straight along
/// the view would make the three faces of an isometric box look the same.
/// Directions are in view space (x right, y up, z towards the viewer) and
/// point the way the light travels.
void SetUpLights(const Handle(V3d_Viewer)& viewer) {
    // Strength is set through the light colour: with sRGB rendering off,
    // OCCT ignores SetIntensity. Levels were measured and solved so the faces
    // of an isometric box read as roughly 0.84 / 0.70 / 0.58 (top / right / front).
    const auto grey = [](double level) { return Quantity_Color(level, level, level, Quantity_TOC_RGB); };
    Handle(V3d_AmbientLight) ambient = new V3d_AmbientLight(grey(0.75));
    Handle(V3d_DirectionalLight) key = new V3d_DirectionalLight(gp_Dir(0.3, -0.75, -0.6), grey(0.37), true);
    Handle(V3d_DirectionalLight) fill = new V3d_DirectionalLight(gp_Dir(-0.8, 0.1, -0.6), grey(0.31), true);
    viewer->AddLight(ambient);
    viewer->AddLight(key);
    viewer->AddLight(fill);
    viewer->SetLightOn();
}

// OpenGL sized formats (not declared by every platform's GL 1.1 header).
constexpr int kGlRgba8           = 0x8058;
constexpr int kGlDepth24Stencil8 = 0x88F0;

/// Aspect_Drawable is a pointer on Windows and macOS but an integer X11 id
/// on Linux; converts the stored handle to whichever it is.
template <class T>
T HandleAs(std::uintptr_t handle) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(handle);
    else return static_cast<T>(handle);
}

gp_Pnt ToPnt(Vec3 v) { return {v.x, v.y, v.z}; }
gp_Dir ToDir(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3   ToVec(const gp_XYZ& p) { return {p.X(), p.Y(), p.Z()}; }

Quantity_Color ToQuantity(const Color& c) {
    return Quantity_Color(static_cast<double>(c.r), static_cast<double>(c.g), static_cast<double>(c.b), Quantity_TOC_sRGB);
}

double SmoothStep(double t) { return t * t * (3.0 - 2.0 * t); }

/// A camera orientation as a unit quaternion: how the camera axes (right,
/// up, back) are turned from the model axes. Interpolating it turns the view
/// along the shortest way, with the up direction staying at a right angle to
/// the line of sight, whatever the two views are (even opposite ones).
struct Orientation {
    double w = 1, x = 0, y = 0, z = 0;

    static Orientation FromView(Vec3 dir, Vec3 up) {
        const Vec3 back = (-dir).Normalized();
        Vec3 right = up.Cross(back);
        if (right.Length() < 1e-9) right = std::abs(back.z) < 0.9 ? Vec3{0, 0, 1}.Cross(back) : Vec3{1, 0, 0}.Cross(back);
        right = right.Normalized();
        const Vec3 u = back.Cross(right);
        // Rotation matrix with columns right, u, back -> quaternion.
        const double m00 = right.x, m01 = u.x, m02 = back.x;
        const double m10 = right.y, m11 = u.y, m12 = back.y;
        const double m20 = right.z, m21 = u.z, m22 = back.z;
        Orientation q;
        const double trace = m00 + m11 + m22;
        if (trace > 0) {
            const double k = 0.5 / std::sqrt(trace + 1.0);
            q = {0.25 / k, (m21 - m12) * k, (m02 - m20) * k, (m10 - m01) * k};
        } else if (m00 > m11 && m00 > m22) {
            const double k = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
            q = {(m21 - m12) / k, 0.25 * k, (m01 + m10) / k, (m02 + m20) / k};
        } else if (m11 > m22) {
            const double k = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
            q = {(m02 - m20) / k, (m01 + m10) / k, 0.25 * k, (m12 + m21) / k};
        } else {
            const double k = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
            q = {(m10 - m01) / k, (m02 + m20) / k, (m12 + m21) / k, 0.25 * k};
        }
        return q.Normalized();
    }

    Orientation Normalized() const {
        const double n = std::sqrt(w * w + x * x + y * y + z * z);
        return {w / n, x / n, y / n, z / n};
    }

    Vec3 Rotate(Vec3 v) const {
        // v' = v + 2w (q x v) + 2 q x (q x v), q = (x, y, z)
        const Vec3 q{x, y, z};
        const Vec3 t = q.Cross(v) * 2.0;
        return v + t * w + q.Cross(t);
    }

    Vec3 Direction() const { return -Rotate({0, 0, 1}); }
    Vec3 Up() const { return Rotate({0, 1, 0}); }

    static Orientation Slerp(Orientation a, Orientation b, double t) {
        double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
        if (dot < 0) {   // the shorter way round
            b = {-b.w, -b.x, -b.y, -b.z};
            dot = -dot;
        }
        if (dot > 0.9995) {
            return Orientation{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}
                .Normalized();
        }
        const double angle = std::acos(dot);
        const double sa = std::sin((1 - t) * angle) / std::sin(angle), sb = std::sin(t * angle) / std::sin(angle);
        return Orientation{a.w * sa + b.w * sb, a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb}.Normalized();
    }
};

/// Sketch frame on a planar face: normal points out of the material, the
/// origin is the world origin projected onto the plane, and X follows the
/// world X axis (or Y when the face faces along X), as users expect.
std::optional<Frame> PlanarFaceFrame(const TopoDS_Face& face) {
    BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) return std::nullopt;
    const gp_Pln pln = surface.Plane();
    Vec3 normal = ToVec(pln.Axis().Direction().XYZ());
    if (face.Orientation() == TopAbs_REVERSED) normal = -normal;

    const Vec3 location = ToVec(pln.Location().XYZ());
    const Vec3 origin = normal * location.Dot(normal);

    const auto projectOnPlane = [&](Vec3 v) { return v - normal * v.Dot(normal); };
    Vec3 xDir = projectOnPlane({1, 0, 0});
    if (xDir.Length() < 1e-6) xDir = projectOnPlane({0, 1, 0});
    return Frame{origin, xDir.Normalized(), normal};
}

} // namespace

// --- Impl -------------------------------------------------------------------

struct Viewport::Impl {
    Handle(OpenGl_GraphicDriver)     driver;
    Handle(V3d_Viewer)               viewer;
    Handle(V3d_View)                 view;
    Handle(AIS_InteractiveContext)   context;
    Handle(Aspect_NeutralWindow)     occtWindow;
    Handle(OpenGl_FrameBuffer)       fbo;

    double width = 1, height = 1, pixelScale = 1;
    int    fbWidth = 1, fbHeight = 1;

    struct Shown {
        ObjectId           body;
        Handle(AIS_Shape)  shape;
    };
    std::vector<Shown> shown;

    /// Measuring (see SetMeasuring).
    struct Measuring {
        bool                           on = false;
        std::vector<TopoDS_Shape>      picked;   ///< at most two
        std::vector<Handle(AIS_Shape)> marks;    ///< the picked entities, highlighted
        Viewport::Measured             result;
    } measuring;

    /// Selection modes of the shown bodies for the current state.
    void ActivateShown() {
        for (const auto& s : shown) {
            context->Deactivate(s.shape);
            if (original.active) continue;
            context->Activate(s.shape, AIS_Shape::SelectionMode(TopAbs_FACE));
            if (measuring.on) {
                context->Activate(s.shape, AIS_Shape::SelectionMode(TopAbs_EDGE));
                context->Activate(s.shape, AIS_Shape::SelectionMode(TopAbs_VERTEX));
            }
        }
    }

    Handle(Graphic3d_ClipPlane) section;        ///< null: no section
    std::optional<Frame>        sectionFrame;   ///< as last set

    std::vector<PlaneQuad>         planes;        ///< as last set
    std::vector<Handle(AIS_Shape)> planeShapes;   ///< their presentations
    std::vector<Handle(AIS_Shape)> overlapShapes;           ///< interferences shown
    std::uint64_t                  overlapRevision = 0;     ///< of the model they were taken from
    std::vector<Handle(AIS_Shape)> copyShapes;              ///< pattern/mirror preview shown
    std::uint64_t                  copyRevision = 0;
    ObjectId                       copyFeature;
    std::uint64_t      syncedRevision = static_cast<std::uint64_t>(-1);
    const kernel::Model* syncedModel = nullptr;
    std::optional<Vec2>  hoverPixel;
    std::optional<kernel::Bounds> bounds;

    ViewportColors colors;
    DisplayStyle   style = DisplayStyle::ShadedWithEdges;
    ObjectId       hovered;
    bool           dirty = true;

    /// Picking on the body as it was before an operation (fillet, shell;
    /// see BeginPickingOriginal).
    struct OriginalPicking {
        bool                            active = false;
        PickTarget                      target = PickTarget::Edges;
        const kernel::Model*            model = nullptr;
        std::uint64_t                   revision = 0;
        std::size_t                     featureIndex = 0;
        std::vector<TopoDS_Shape>       bodies;    ///< the bodies before the operation
        std::vector<Handle(AIS_Shape)>  targets;   ///< their edges as lines, selectable
        std::vector<doc::EdgeReference> markedEdges;
        std::vector<doc::FaceReference> markedFaces;
        std::vector<Handle(AIS_Shape)>  marked;    ///< chosen edges (thick) and faces (tinted)
        std::uint64_t                   markedRevision = static_cast<std::uint64_t>(-1);
        std::uint64_t                   shownRevision = static_cast<std::uint64_t>(-1);
    } original;

    /// Shows the chosen edges and faces on the most recent original body.
    void ShowMarked() {
        OriginalPicking& o = original;
        for (const auto& h : o.marked) context->Remove(h, false);
        o.marked.clear();
        o.markedRevision = o.active ? o.revision : 0;
        dirty = true;
        if (!o.active || o.bodies.empty()) return;
        const TopoDS_Shape& body = o.bodies.back();   // where the operation applies
        for (const auto& edge : kernel::occt::ResolveEdges(o.markedEdges, body)) {
            if (!edge) continue;
            Handle(AIS_Shape) ais = new AIS_Shape(*edge);
            ais->SetColor(ToQuantity(colors.selection));
            ais->SetWidth(3.5);
            ais->SetZLayer(Graphic3d_ZLayerId_Topmost);
            context->Display(ais, AIS_WireFrame, -1, false);
            o.marked.push_back(ais);
        }
        for (const doc::FaceReference& ref : o.markedFaces) {
            const std::optional<TopoDS_Face> face = kernel::occt::ResolveFace(body, ref);
            if (!face) continue;
            Handle(AIS_Shape) ais = new AIS_Shape(*face);
            ais->SetColor(ToQuantity(colors.selection));
            ais->SetTransparency(0.75);   // a tint: the preview behind stays readable
            ais->Attributes()->SetFaceBoundaryDraw(false);
            ais->SetZLayer(Graphic3d_ZLayerId_Topmost);
            context->Display(ais, AIS_Shaded, -1, false);
            o.marked.push_back(ais);
        }
    }

    bool        animating = false;
    CameraState animFrom, animTo;
    double      animStart = 0, animDuration = 0;

    Handle(Graphic3d_Camera) Camera() const { return view->Camera(); }

    Handle(OpenGl_Context) GlContext() const { return driver->GetSharedContext(); }

    void ApplyStyle(const Handle(AIS_Shape)& ais, bool hoveredBody) const {
        Handle(Prs3d_Drawer) drawer = ais->Attributes();
        const Color body = hoveredBody ? colors.hover : colors.body;
        ais->SetColor(ToQuantity(body));
        // Matte with a faint highlight. The ambient part is the body colour
        // itself, so faces turned away from the lights stay readable.
        Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_Plastified);
        material.SetColor(ToQuantity(body));
        material.SetAmbientColor(ToQuantity(body));
        material.SetSpecularColor(Quantity_Color(0.12, 0.12, 0.12, Quantity_TOC_RGB));
        ais->SetMaterial(material);

        const bool edges = style == DisplayStyle::ShadedWithEdges || style == DisplayStyle::Transparent;
        drawer->SetFaceBoundaryDraw(edges);
        // Sharp edges and the tangent edges where a fillet starts and ends
        // (they show its size); not the seam along a cylinder, which is
        // smooth beyond tangency.
        drawer->SetFaceBoundaryUpperContinuity(GeomAbs_C1);
        // Finer tessellation than OCCT's default (20°): round faces look
        // round; coarser on the lower quality levels.
        static constexpr double kAngle[] = {4.0, 8.0, 15.0}, kCoefficient[] = {0.0005, 0.001, 0.003};
        drawer->SetDeviationAngle(kAngle[quality] * kPi / 180.0);
        drawer->SetDeviationCoefficient(kCoefficient[quality]);
        drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(ToQuantity(colors.edges), Aspect_TOL_SOLID, 1.0));
        // Wireframe and hidden-line styles draw edges in the edge colour (not
        // the body colour SetColor gave them) and no isolines.
        const Quantity_Color ink = ToQuantity(colors.edges);
        drawer->SetWireAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetLineAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetFreeBoundaryAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetUnFreeBoundaryAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetUIsoAspect(new Prs3d_IsoAspect(ink, Aspect_TOL_SOLID, 1.0, 0));
        drawer->SetVIsoAspect(new Prs3d_IsoAspect(ink, Aspect_TOL_SOLID, 1.0, 0));
        // "Скрытые линии": only the visible edges, in the edge colour.
        drawer->SetSeenLineAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        ais->SetTransparency(style == DisplayStyle::Transparent ? 0.55 : 0.0);

        const bool wire = style == DisplayStyle::Wireframe || style == DisplayStyle::HiddenLines;
        context->SetDisplayMode(ais, wire ? AIS_WireFrame : AIS_Shaded, false);
    }

    int quality = 0;   ///< see Viewport::SetQuality

    void ApplyStyleAll() {
        for (const Shown& s : shown) {
            ApplyStyle(s.shape, s.body == hovered);
            context->Redisplay(s.shape, false);
        }
        view->SetComputedMode(style == DisplayStyle::HiddenLines);
        dirty = true;
    }

    /// Point on the plane through the camera centre, under a screen pixel.
    Vec3 FocalPoint(Vec2 pixel) const {
        const Handle(Graphic3d_Camera) cam = Camera();
        const Vec3 center = ToVec(cam->Center().XYZ());
        const Vec3 up = ToVec(cam->Up().XYZ());
        const Vec3 dir = ToVec(cam->Direction().XYZ());
        const Vec3 right = dir.Cross(up).Normalized();
        const double mmpp = cam->Scale() / height;
        return center + right * ((pixel.x - width / 2) * mmpp) + up * ((height / 2 - pixel.y) * mmpp);
    }
};

// --- Viewport ---------------------------------------------------------------

Viewport::Viewport() : myImpl(std::make_unique<Impl>()) {}

Viewport::~Viewport() {
    Impl& m = *myImpl;
    if (!m.fbo.IsNull() && !m.driver.IsNull()) m.fbo->Release(m.GlContext().get());
}

bool Viewport::Init(const NativeGlSurface& surface) {
    Impl& m = *myImpl;

    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
    m.driver = new OpenGl_GraphicDriver(display, false);
    m.driver->ChangeOptions().buffersNoSwap = true;
    // Render plain sRGB colours into our RGBA8 texture. OCCT's linear-light
    // pipeline relies on an sRGB-capable output, which the window may not
    // have (often on Windows); the result would differ per platform.
    m.driver->ChangeOptions().sRGBDisable = true;

    m.viewer = new V3d_Viewer(m.driver);
    SetUpLights(m.viewer);
    m.context = new AIS_InteractiveContext(m.viewer);
    m.context->SetPixelTolerance(7);   // edges are thin: give the cursor a few pixels

    // Pre-highlight (hover) and selection styles: a light-blue tint.
    for (auto type : {Prs3d_TypeOfHighlight_LocalDynamic, Prs3d_TypeOfHighlight_Dynamic}) {
        Handle(Prs3d_Drawer) s = m.context->HighlightStyle(type);
        s->SetColor(ToQuantity(m.colors.hover));
        s->SetDisplayMode(AIS_Shaded);
        s->SetTransparency(0.6f);   // a soft tint: the body colour shows through
        // In the body's own layer: the part of a face hidden behind a wall
        // stays hidden (the default overlay layer shows it through the wall).
        s->SetZLayer(Graphic3d_ZLayerId_UNKNOWN);
        // A hovered edge (fillet, chamfer) is drawn thick enough to see.
        const Handle(Prs3d_LineAspect) thick = new Prs3d_LineAspect(ToQuantity(m.colors.hover), Aspect_TOL_SOLID, 3.5);
        s->SetWireAspect(thick);
        s->SetLineAspect(thick);
        s->SetUnFreeBoundaryAspect(thick);
        s->SetFreeBoundaryAspect(thick);
    }
    for (auto type : {Prs3d_TypeOfHighlight_LocalSelected, Prs3d_TypeOfHighlight_Selected}) {
        Handle(Prs3d_Drawer) s = m.context->HighlightStyle(type);
        s->SetColor(ToQuantity(m.colors.selection));
        s->SetDisplayMode(AIS_Shaded);
        s->SetTransparency(0.25f);
    }

    m.view = m.viewer->CreateView();
    m.view->SetImmediateUpdate(false);
    m.view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    Graphic3d_RenderingParams& params = m.view->ChangeRenderingParams();
    params.NbMsaaSamples = 4;
    // Plain blending: the tints here are light, and order-independent
    // transparency costs several passes over the whole scene every frame.
    params.TransparencyMethod = Graphic3d_RTM_BLEND_UNORDERED;
    params.IsAntialiasingEnabled = true;

    m.occtWindow = new Aspect_NeutralWindow();
    m.occtWindow->SetVirtual(true);
    m.occtWindow->SetSize(m.fbWidth, m.fbHeight);
    m.occtWindow->SetNativeHandle(HandleAs<Aspect_Drawable>(surface.window));
    const auto glContext = static_cast<Aspect_RenderingContext>(surface.glContext);
    if (!glContext) {
        log::Error("Viewport: no native OpenGL context");
        return false;
    }
    m.view->SetWindow(m.occtWindow, glContext);
    SetColors(m.colors);
    SetCamera({});
    log::Info("Viewport: OpenGL ready");
    return true;
}

void Viewport::SetSize(double width, double height, double pixelScale) {
    Impl& m = *myImpl;
    width = std::max(width, 1.0);
    height = std::max(height, 1.0);
    if (width == m.width && height == m.height && pixelScale == m.pixelScale) return;
    m.width = width;
    m.height = height;
    m.pixelScale = pixelScale;
    m.fbWidth = std::max(1, static_cast<int>(std::lround(width * pixelScale)));
    m.fbHeight = std::max(1, static_cast<int>(std::lround(height * pixelScale)));
    m.occtWindow->SetSize(m.fbWidth, m.fbHeight);
    m.view->MustBeResized();
    m.dirty = true;
}

void Viewport::Sync(const kernel::Model& model) {
    Impl& m = *myImpl;
    if (&model == m.syncedModel && model.Revision() == m.syncedRevision) return;
    m.syncedModel = &model;
    m.syncedRevision = model.Revision();

    for (const auto& s : m.shown) m.context->Remove(s.shape, false);
    m.shown.clear();
    for (const kernel::Body& body : model.GetImpl().bodies) {
        Handle(AIS_Shape) ais = new AIS_Shape(body.shape);
        m.ApplyStyle(ais, body.id == m.hovered);
        m.context->Display(ais, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
        m.shown.push_back({body.id, ais});
    }
    m.ActivateShown();
    // Picked entities belonged to the old shapes.
    if (!m.measuring.picked.empty()) ClearMeasure();
    m.bounds = model.BoundingBox();
    m.view->SetComputedMode(m.style == DisplayStyle::HiddenLines);
    m.dirty = true;
}

void Viewport::Hover(std::optional<Vec2> pixel) {
    Impl& m = *myImpl;
    if (pixel == m.hoverPixel) return;
    m.hoverPixel = pixel;
    if (!pixel) {
        if (m.context->HasDetected()) {
            m.context->ClearDetected(false);
            m.dirty = true;
        }
        return;
    }
    const auto px = static_cast<int>(std::lround(pixel->x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel->y * m.pixelScale));
    // Redraw only when what is under the cursor changed: moving across one
    // face must not repaint the whole model on every mouse event.
    const Handle(SelectMgr_EntityOwner) before = m.context->HasDetected() ? m.context->DetectedOwner() : nullptr;
    m.context->MoveTo(px, py, m.view, false);
    const Handle(SelectMgr_EntityOwner) after = m.context->HasDetected() ? m.context->DetectedOwner() : nullptr;
    if (before != after) m.dirty = true;
}

void Viewport::SetColors(const ViewportColors& colors) {
    Impl& m = *myImpl;
    m.colors = colors;
    m.view->SetBgGradientColors(ToQuantity(colors.backgroundTop), ToQuantity(colors.backgroundBottom),
                                Aspect_GradientFillMethod_Vertical, false);
    for (auto type : {Prs3d_TypeOfHighlight_LocalDynamic, Prs3d_TypeOfHighlight_Dynamic})
        m.context->HighlightStyle(type)->SetColor(ToQuantity(colors.hover));
    for (auto type : {Prs3d_TypeOfHighlight_LocalSelected, Prs3d_TypeOfHighlight_Selected})
        m.context->HighlightStyle(type)->SetColor(ToQuantity(colors.selection));
    m.ApplyStyleAll();
}

void Viewport::SetDisplayStyle(DisplayStyle style) {
    if (myImpl->style == style) return;
    myImpl->style = style;
    myImpl->ApplyStyleAll();
}

void Viewport::SetQuality(int level) {
    Impl& m = *myImpl;
    level = std::clamp(level, 0, 2);
    if (m.quality == level) return;
    m.quality = level;
    static constexpr int kSamples[] = {4, 2, 0};
    m.view->ChangeRenderingParams().NbMsaaSamples = kSamples[level];
    // New tessellation: the old triangles stay on the shape until cleaned.
    for (const auto& s : m.shown) {
        BRepTools::Clean(s.shape->Shape());
        s.shape->SetToUpdate();
    }
    m.ApplyStyleAll();
}

void Viewport::SetHoveredBody(ObjectId body) {
    if (myImpl->hovered == body) return;
    myImpl->hovered = body;
    myImpl->ApplyStyleAll();
}

void Viewport::Render() {
    Impl& m = *myImpl;
    const Handle(OpenGl_Context) gl = m.GlContext();
    if (gl.IsNull()) {
        // The OpenGL context is created lazily by the first redraw.
        m.view->Redraw();
    }
    const Handle(OpenGl_Context) ctx = m.GlContext();
    if (ctx.IsNull()) return;

    if (m.fbo.IsNull() || m.fbo->GetVPSizeX() != m.fbWidth || m.fbo->GetVPSizeY() != m.fbHeight) {
        if (m.fbo.IsNull()) m.fbo = new OpenGl_FrameBuffer();
        m.fbo->Release(ctx.get());
        if (!m.fbo->Init(ctx, Graphic3d_Vec2i(m.fbWidth, m.fbHeight), kGlRgba8, kGlDepth24Stencil8)) {
            log::Error("Viewport: cannot create a {}x{} framebuffer", m.fbWidth, m.fbHeight);
            return;
        }
        m.view->View()->SetFBO(m.fbo);
        m.view->MustBeResized();
    }
    m.view->Redraw();
    m.fbo->UnbindBuffer(ctx);
    m.dirty = false;
}

unsigned Viewport::TextureId() const {
    const Impl& m = *myImpl;
    if (m.fbo.IsNull() || m.fbo->ColorTexture().IsNull()) return 0;
    return m.fbo->ColorTexture()->TextureId();
}

bool Viewport::NeedsRender() const { return myImpl->dirty || myImpl->animating; }

ViewProjection Viewport::Projection() const {
    const Impl& m = *myImpl;
    const Handle(Graphic3d_Camera) cam = m.Camera();
    cam->SetAspect(m.width / m.height);
    const Graphic3d_Mat4d vp = cam->ProjectionMatrix() * cam->OrientationMatrix();
    ViewProjection out;
    for (std::size_t r = 0; r < 4; ++r)
        for (std::size_t c = 0; c < 4; ++c)
            out.matrix[r * 4 + c] = vp.GetValue(static_cast<unsigned>(r), static_cast<unsigned>(c));
    out.width = m.width;
    out.height = m.height;
    out.direction = ToVec(cam->Direction().XYZ());
    out.up = ToVec(cam->Up().XYZ());
    out.mmPerPixel = cam->Scale() / m.height;
    return out;
}

CameraState Viewport::Camera() const {
    const Handle(Graphic3d_Camera) cam = myImpl->Camera();
    return {ToVec(cam->Eye().XYZ()), ToVec(cam->Center().XYZ()), ToVec(cam->Up().XYZ()), cam->Scale()};
}

void Viewport::SetCamera(const CameraState& state) {
    const Handle(Graphic3d_Camera) cam = myImpl->Camera();
    cam->SetEyeAndCenter(ToPnt(state.eye), ToPnt(state.center));
    cam->SetUp(ToDir(state.up));
    cam->OrthogonalizeUp();   // an up leaning toward the line of sight would tilt the next turns
    cam->SetScale(std::max(state.scale, 1e-3));
    myImpl->dirty = true;
}

void Viewport::Orbit(double yaw, double pitch, Vec3 pivot) {
    CameraState c = Camera();
    const Vec3 dir = (c.center - c.eye).Normalized();
    const Vec3 right = dir.Cross(c.up).Normalized();

    gp_Trsf pitchRot, yawRot;
    pitchRot.SetRotation(gp_Ax1(ToPnt(pivot), ToDir(right)), -pitch);
    yawRot.SetRotation(gp_Ax1(ToPnt(pivot), gp::DZ()), -yaw);
    const gp_Trsf t = yawRot.Multiplied(pitchRot);

    const gp_Pnt eye = ToPnt(c.eye).Transformed(t);
    const gp_Pnt center = ToPnt(c.center).Transformed(t);
    const gp_Dir up = ToDir(c.up).Transformed(t);
    SetCamera({ToVec(eye.XYZ()), ToVec(center.XYZ()), ToVec(up.XYZ()), c.scale});
}

void Viewport::Pan(double dx, double dy) {
    CameraState c = Camera();
    const Vec3 dir = (c.center - c.eye).Normalized();
    const Vec3 right = dir.Cross(c.up).Normalized();
    const double mmpp = c.scale / myImpl->height;
    const Vec3 shift = right * (-dx * mmpp) + c.up * (dy * mmpp);
    SetCamera({c.eye + shift, c.center + shift, c.up, c.scale});
}

void Viewport::ZoomAt(double factor, Vec2 pixel) {
    Impl& m = *myImpl;
    const Vec3 before = m.FocalPoint(pixel);
    CameraState c = Camera();
    c.scale = std::clamp(c.scale / factor, 1e-3, 1e7);
    SetCamera(c);
    const Vec3 after = m.FocalPoint(pixel);
    const Vec3 shift = before - after;
    SetCamera({c.eye + shift, c.center + shift, c.up, c.scale});
}

CameraState Viewport::FitCamera(Vec3 direction, Vec3 up, const Box3& extra, double margin) const {
    const Impl& m = *myImpl;
    Box3 box = extra;
    if (m.bounds) {
        box.Include(m.bounds->min);
        box.Include(m.bounds->max);
    }
    // Empty part: frame the origin planes the UI draws (40 mm half-size at
    // least, see PlaneHalfSize in the viewport panel) with a little air.
    if (box.IsEmpty()) box = {{-45, -45, -45}, {45, 45, 45}};
    const Vec3 lo = box.min, hi = box.max;
    const Vec3 center = (lo + hi) * 0.5;
    const Vec3 dir = direction.Normalized();
    const Vec3 right = dir.Cross(up).Normalized();
    const Vec3 trueUp = right.Cross(dir).Normalized();

    double w = 0, h = 0;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
        const Vec3 d = corner - center;
        w = std::max(w, std::abs(d.Dot(right)) * 2);
        h = std::max(h, std::abs(d.Dot(trueUp)) * 2);
    }
    const double aspect = m.width / m.height;
    const double scale = std::max({h, w / aspect, 1.0}) * margin;
    const double distance = (hi - lo).Length() * 2 + 100;
    return {center - dir * distance, center, trueUp, scale};
}

Box3 Viewport::ModelBounds() const {
    Box3 box;
    if (myImpl->bounds) {
        box.Include(myImpl->bounds->min);
        box.Include(myImpl->bounds->max);
    }
    return box;
}

Vec3 Viewport::ModelCenter() const {
    const Impl& m = *myImpl;
    if (!m.bounds) return {};
    return (m.bounds->min + m.bounds->max) * 0.5;
}

void Viewport::AnimateTo(const CameraState& target, double seconds) {
    Impl& m = *myImpl;
    if (seconds <= 0) {
        m.animating = false;
        SetCamera(target);
        return;
    }
    m.animFrom = Camera();
    m.animTo = target;
    m.animStart = glfwGetTime();
    m.animDuration = seconds;
    m.animating = true;
}

bool Viewport::StepAnimation(double now) {
    Impl& m = *myImpl;
    if (!m.animating) return false;
    const double t = std::clamp((now - m.animStart) / m.animDuration, 0.0, 1.0);
    const double s = SmoothStep(t);

    const Orientation from = Orientation::FromView(m.animFrom.center - m.animFrom.eye, m.animFrom.up);
    const Orientation to = Orientation::FromView(m.animTo.center - m.animTo.eye, m.animTo.up);
    const Orientation turned = Orientation::Slerp(from, to, s);
    const double fromDist = (m.animFrom.center - m.animFrom.eye).Length();
    const double toDist = (m.animTo.center - m.animTo.eye).Length();
    const Vec3 dir = turned.Direction();
    const Vec3 center = m.animFrom.center + (m.animTo.center - m.animFrom.center) * s;
    const Vec3 up = turned.Up();
    const double dist = fromDist + (toDist - fromDist) * s;
    const double scale = m.animFrom.scale * std::pow(m.animTo.scale / m.animFrom.scale, s);
    SetCamera({center - dir * dist, center, up, scale});

    if (t >= 1.0) {
        m.animating = false;
        SetCamera(m.animTo);
    }
    return m.animating;
}

std::optional<PickResult> Viewport::Pick(Vec2 pixel) {
    Impl& m = *myImpl;
    if (m.shown.empty()) return std::nullopt;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    if (selector->NbPicked() < 1) return std::nullopt;

    PickResult result;
    result.point = ToVec(selector->PickedPoint(1).XYZ());
    const Handle(SelectMgr_EntityOwner) owner = selector->Picked(1);
    for (const auto& s : m.shown)
        if (owner->Selectable() == s.shape) result.body = s.body;

    if (const Handle(StdSelect_BRepOwner) brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
        !brep.IsNull() && brep->Shape().ShapeType() == TopAbs_FACE)
        result.plane = PlanarFaceFrame(TopoDS::Face(brep->Shape()));
    // The face was clicked from where the camera is: its normal points
    // that way (a face's stored orientation is not reliable for this), so
    // a sketch on it is seen from the front, never from behind the part.
    if (result.plane) {
        double ex = 0, ey = 0, ez = 0;
        m.view->Proj(ex, ey, ez);   // towards the eye
        if (result.plane->normal.Dot(Vec3{ex, ey, ez}) < 0) result.plane->normal = -result.plane->normal;
    }
    return result;
}

// --- measuring ------------------------------------------------------------------

void Viewport::SetMeasuring(bool on) {
    Impl& m = *myImpl;
    if (m.measuring.on == on) return;
    m.measuring.on = on;
    if (!on) ClearMeasure();
    m.ActivateShown();
    m.hoverPixel.reset();
    m.dirty = true;
}

bool Viewport::MeasurePick(Vec2 pixel) {
    Impl& m = *myImpl;
    Impl::Measuring& ms = m.measuring;
    if (!ms.on) return false;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    if (selector->NbPicked() < 1) return false;
    const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(1));
    if (owner.IsNull()) return false;
    if (ms.picked.size() >= 2) ClearMeasure();   // a third click starts over

    const TopoDS_Shape entity = owner->Shape();
    const kernel::EntityInfo info = kernel::occt::Describe(entity, ToVec(selector->PickedPoint(1).XYZ()));
    ms.picked.push_back(entity);
    (ms.picked.size() == 1 ? ms.result.first : ms.result.second) = info;
    if (ms.picked.size() == 2) ms.result.between = kernel::occt::Measure(ms.picked[0], ms.picked[1]);

    Handle(AIS_Shape) mark = new AIS_Shape(entity);
    mark->SetColor(ToQuantity(m.colors.selection));
    mark->SetWidth(3.5);
    mark->SetTransparency(entity.ShapeType() == TopAbs_FACE ? 0.6 : 0.0);
    mark->Attributes()->SetFaceBoundaryDraw(false);
    mark->SetZLayer(Graphic3d_ZLayerId_Topmost);
    m.context->Display(mark, entity.ShapeType() == TopAbs_FACE ? AIS_Shaded : AIS_WireFrame, -1, false);
    ms.marks.push_back(mark);
    m.dirty = true;
    return true;
}

void Viewport::ClearMeasure() {
    Impl& m = *myImpl;
    for (const auto& mark : m.measuring.marks) m.context->Remove(mark, false);
    m.measuring.marks.clear();
    m.measuring.picked.clear();
    m.measuring.result = {};
    m.dirty = true;
}

const Viewport::Measured& Viewport::Measurement() const { return myImpl->measuring.result; }

// --- section --------------------------------------------------------------------

void Viewport::ShowInterferences(const kernel::Model* model) {
    Impl& m = *myImpl;
    const std::uint64_t revision = model ? model->Revision() + 1 : 0;   // 0: hidden
    if (revision == m.overlapRevision) return;
    m.overlapRevision = revision;
    for (const auto& s : m.overlapShapes) m.context->Remove(s, false);
    m.overlapShapes.clear();
    m.dirty = true;
    if (!model) return;
    model->Interferences();
    for (const TopoDS_Shape& overlap : model->GetImpl().overlaps) {
        Handle(AIS_Shape) ais = new AIS_Shape(overlap);
        ais->SetColor(ToQuantity(m.colors.interference));
        ais->SetTransparency(0.25);
        ais->SetZLayer(Graphic3d_ZLayerId_Topmost);   // seen through the bodies around it
        m.context->Display(ais, AIS_Shaded, -1, false);
        m.overlapShapes.push_back(ais);
    }
}

void Viewport::ShowCopyPreview(const kernel::Model* model, ObjectId feature) {
    Impl& m = *myImpl;
    const std::uint64_t revision = model ? model->Revision() + 1 : 0;
    if (revision == m.copyRevision && feature == m.copyFeature) return;
    m.copyRevision = revision;
    m.copyFeature = feature;
    for (const auto& s : m.copyShapes) m.context->Remove(s, false);
    m.copyShapes.clear();
    m.dirty = true;
    if (!model) return;
    const auto& previews = model->GetImpl().copyPreviews;
    const auto it = previews.find(feature);
    if (it == previews.end()) return;
    const auto show = [&](const TopoDS_Shape& shape, const Color& color, double width) {
        Handle(AIS_Shape) ais = new AIS_Shape(shape);
        ais->SetColor(ToQuantity(color));
        ais->SetWidth(width);
        ais->SetZLayer(Graphic3d_ZLayerId_Topmost);   // seen through the body
        m.context->Display(ais, AIS_WireFrame, -1, false);
        m.copyShapes.push_back(ais);
    };
    for (const TopoDS_Shape& s : it->second.sources) show(s, m.colors.selection, 2.5);
    for (const TopoDS_Shape& s : it->second.copies) show(s, m.colors.hover, 1.6);
}

void Viewport::SetSection(const std::optional<Frame>& plane) {
    Impl& m = *myImpl;
    const bool same = plane.has_value() == m.sectionFrame.has_value() &&
                      (!plane || ((plane->origin - m.sectionFrame->origin).Length() < 1e-9 &&
                                  (plane->normal - m.sectionFrame->normal).Length() < 1e-9));
    if (same) return;
    m.sectionFrame = plane;
    if (!plane) {
        if (!m.section.IsNull()) m.view->RemoveClipPlane(m.section);
        m.section.Nullify();
        m.dirty = true;
        return;
    }
    if (m.section.IsNull()) {
        m.section = new Graphic3d_ClipPlane();
        // Filled cut faces in the body colour, a little darker, so the cut reads as solid material.
        m.section->SetCapping(true);
        Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_UserDefined);
        const Color body = m.colors.body;
        material.SetColor(Quantity_Color(body.r * 0.82, body.g * 0.82, body.b * 0.82, Quantity_TOC_sRGB));
        m.section->SetCappingMaterial(material);
        m.view->AddClipPlane(m.section);
    }
    // OCCT keeps the side the equation is positive on: flip the normal so
    // what lies ahead of `normal` is cut away.
    const Vec3 n = -plane->normal;
    m.section->SetEquation(gp_Pln(gp_Pnt(plane->origin.x, plane->origin.y, plane->origin.z), gp_Dir(n.x, n.y, n.z)));
    m.dirty = true;
}

// --- planes -------------------------------------------------------------------

void Viewport::SetPlanes(const std::vector<PlaneQuad>& planes) {
    Impl& m = *myImpl;
    if (planes == m.planes) return;
    m.planes = planes;
    for (const auto& p : m.planeShapes) m.context->Remove(p, false);
    m.planeShapes.clear();
    for (const PlaneQuad& q : planes) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const Vec3& c : q.corners) polygon.Add(gp_Pnt(c.x, c.y, c.z));
        polygon.Close();
        const BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
        if (!face.IsDone()) continue;
        Handle(AIS_Shape) ais = new AIS_Shape(face.Face());
        ais->SetColor(ToQuantity(q.fill));
        ais->SetMaterial(Graphic3d_MaterialAspect(Graphic3d_NameOfMaterial_UserDefined));
        ais->SetTransparency(1.0 - q.fill.a);
        Handle(Prs3d_Drawer) drawer = ais->Attributes();
        drawer->SetFaceBoundaryDraw(q.edgeWidth > 0);   // 0: the outline is drawn by the UI instead
        if (q.edgeWidth > 0) drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(ToQuantity(q.edge), Aspect_TOL_SOLID, q.edgeWidth));
        m.context->Display(ais, AIS_Shaded, -1, false);
        m.planeShapes.push_back(ais);
    }
    m.dirty = true;
}

// --- picking on the original body -----------------------------------------

void Viewport::BeginPickingOriginal(const kernel::Model& model, std::size_t featureIndex, PickTarget target) {
    Impl& m = *myImpl;
    Impl::OriginalPicking& o = m.original;

    // The result stays on screen as a live preview, but only the body as it
    // was is pickable: its edges are drawn as lines over the preview, so an
    // edge already rounded off or a face already opened is still there to
    // click (again to drop it).
    if (!o.active || o.shownRevision != m.syncedRevision) {
        for (const auto& s : m.shown) m.context->Deactivate(s.shape);
        o.shownRevision = m.syncedRevision;
    }
    if (o.active && o.model == &model && o.revision == model.Revision() && o.featureIndex == featureIndex && o.target == target)
        return;

    for (const auto& t : o.targets) m.context->Remove(t, false);
    o.targets.clear();
    o.bodies.clear();
    const auto& after = model.GetImpl().bodiesAfter;
    if (featureIndex > 0 && featureIndex <= after.size())
        for (const kernel::Body& body : after[featureIndex - 1]) o.bodies.push_back(body.shape);
    const int mode = AIS_Shape::SelectionMode(target == PickTarget::Faces      ? TopAbs_FACE
                                              : target == PickTarget::Vertices ? TopAbs_VERTEX
                                                                               : TopAbs_EDGE);
    for (const TopoDS_Shape& body : o.bodies) {
        // Shaded (a wireframe presentation is never found by the selector),
        // but with fully transparent faces: only its edges show, over the
        // preview, in the edge colour.
        Handle(AIS_Shape) ais = new AIS_Shape(body);
        m.ApplyStyle(ais, false);
        Handle(Prs3d_Drawer) drawer = ais->Attributes();
        drawer->SetFaceBoundaryDraw(true);
        drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(ToQuantity(m.colors.edges), Aspect_TOL_SOLID, 1.2));
        ais->SetTransparency(1.0);
        m.context->Display(ais, AIS_Shaded, mode, false);
        if (target == PickTarget::EdgesAndFaces) m.context->Activate(ais, AIS_Shape::SelectionMode(TopAbs_FACE));
        o.targets.push_back(ais);
    }
    o.active = true;
    o.target = target;
    o.model = &model;
    o.revision = model.Revision();
    o.featureIndex = featureIndex;
    m.ShowMarked();   // re-resolve against the new bodies
}

void Viewport::EndPickingOriginal() {
    Impl& m = *myImpl;
    Impl::OriginalPicking& o = m.original;
    if (!o.active) return;
    for (const auto& t : o.targets) m.context->Remove(t, false);
    for (const auto& h : o.marked) m.context->Remove(h, false);
    o = {};
    m.ActivateShown();
    m.hoverPixel.reset();
    m.dirty = true;
}

std::optional<doc::EdgeReference> Viewport::PickEdge(Vec2 pixel) {
    Impl& m = *myImpl;
    if (!m.original.active) return std::nullopt;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(i));
        // With faces pickable too, a face in front hides the edges behind it.
        if (!owner.IsNull() && owner->Shape().ShapeType() == TopAbs_FACE && m.original.target == PickTarget::EdgesAndFaces)
            return std::nullopt;
        if (owner.IsNull() || owner->Shape().ShapeType() != TopAbs_EDGE) continue;
        for (std::size_t b = 0; b < m.original.targets.size(); ++b)
            if (owner->Selectable() == m.original.targets[b])
                return kernel::occt::DescribeEdge(TopoDS::Edge(owner->Shape()), m.original.bodies[b]);
    }
    return std::nullopt;
}

std::vector<doc::EdgeReference> Viewport::PickFaceEdges(Vec2 pixel) {
    Impl& m = *myImpl;
    std::vector<doc::EdgeReference> out;
    if (!m.original.active) return out;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    if (selector->NbPicked() < 1) return out;
    // Only when the nearest thing is a face (an edge nearby wins).
    const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(1));
    if (owner.IsNull() || owner->Shape().ShapeType() != TopAbs_FACE) return out;
    for (std::size_t b = 0; b < m.original.targets.size(); ++b) {
        if (owner->Selectable() != m.original.targets[b]) continue;
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(owner->Shape(), TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent(); ++i)
            out.push_back(kernel::occt::DescribeEdge(TopoDS::Edge(edges(i)), m.original.bodies[b]));
    }
    return out;
}

std::optional<Viewport::CylinderPick> Viewport::PickCylinder(Vec2 pixel) {
    Impl& m = *myImpl;
    if (!m.original.active || m.original.target != PickTarget::Faces) return std::nullopt;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(i));
        if (owner.IsNull() || owner->Shape().ShapeType() != TopAbs_FACE) continue;
        const BRepAdaptor_Surface surface(TopoDS::Face(owner->Shape()));
        if (surface.GetType() != GeomAbs_Cylinder) return std::nullopt;   // the nearest face is not a cylinder
        const gp_Cylinder cyl = surface.Cylinder();
        const gp_Ax1 axis = cyl.Axis();
        const gp_Pnt hit = selector->PickedPoint(i);
        const Vec3 base = ToVec(axis.Location().XYZ()), dir = ToVec(axis.Direction().XYZ()), p = ToVec(hit.XYZ());
        Vec3 radial = (p - base) - dir * (p - base).Dot(dir);
        if (radial.Length() < 1e-9) return std::nullopt;
        return CylinderPick{base, dir, radial.Normalized(), cyl.Radius()};
    }
    return std::nullopt;
}

std::optional<Viewport::VertexPick> Viewport::PickVertex(Vec2 pixel) {
    Impl& m = *myImpl;
    if (!m.original.active || m.original.target != PickTarget::Vertices) return std::nullopt;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(i));
        if (owner.IsNull() || owner->Shape().ShapeType() != TopAbs_VERTEX) continue;
        const TopoDS_Vertex& vertex = TopoDS::Vertex(owner->Shape());
        VertexPick pick{ToVec(BRep_Tool::Pnt(vertex).XYZ()), std::nullopt};
        for (auto body = m.original.bodies.rbegin(); body != m.original.bodies.rend() && !pick.edge; ++body)
            pick.edge = kernel::occt::DescribeVertexEdge(vertex, *body);
        return pick;
    }
    return std::nullopt;
}

std::optional<doc::FaceReference> Viewport::PickFace(Vec2 pixel) {
    Impl& m = *myImpl;
    if (!m.original.active) return std::nullopt;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(i));
        if (owner.IsNull() || owner->Shape().ShapeType() != TopAbs_FACE) continue;
        const std::optional<Vec3> normal = kernel::occt::PlanarNormal(TopoDS::Face(owner->Shape()));
        if (!normal) return std::nullopt;   // the nearest face is curved
        return doc::FaceReference{*normal, ToVec(selector->PickedPoint(i).XYZ())};
    }
    return std::nullopt;
}

bool Viewport::SameOriginalFace(const doc::FaceReference& a, const doc::FaceReference& b) const {
    const Impl::OriginalPicking& o = myImpl->original;
    if (o.bodies.empty()) return false;
    const auto fa = kernel::occt::ResolveFace(o.bodies.back(), a);
    const auto fb = kernel::occt::ResolveFace(o.bodies.back(), b);
    return fa && fb && fa->IsSame(*fb);
}

void Viewport::SetMarkedEdges(const std::vector<doc::EdgeReference>& edges) {
    Impl& m = *myImpl;
    Impl::OriginalPicking& o = m.original;
    if (edges == o.markedEdges && (o.active ? o.revision : 0) == o.markedRevision) return;
    o.markedEdges = edges;
    m.ShowMarked();
}

void Viewport::SetMarkedFaces(const std::vector<doc::FaceReference>& faces) {
    Impl& m = *myImpl;
    Impl::OriginalPicking& o = m.original;
    if (faces == o.markedFaces && (o.active ? o.revision : 0) == o.markedRevision) return;
    o.markedFaces = faces;
    m.ShowMarked();
}

} // namespace bz::render
