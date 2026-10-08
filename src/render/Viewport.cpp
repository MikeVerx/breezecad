#include "render/Viewport.h"

#include "core/Log.h"
#include "kernel/occt/ModelImpl.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Mat4d.hxx>
#include <Graphic3d_Vec2.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_Texture.hxx>
#include <Prs3d_Drawer.hxx>
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

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_NSGL
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#endif
#include <GLFW/glfw3native.h>

#include <algorithm>
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
    Handle(V3d_AmbientLight) ambient = new V3d_AmbientLight(Quantity_NOC_WHITE);
    ambient->SetIntensity(0.55f);
    Handle(V3d_DirectionalLight) key = new V3d_DirectionalLight(gp_Dir(0.3, -0.75, -0.6), Quantity_NOC_WHITE, true);
    key->SetIntensity(0.55f);
    Handle(V3d_DirectionalLight) fill = new V3d_DirectionalLight(gp_Dir(-0.8, 0.1, -0.6), Quantity_NOC_WHITE, true);
    fill->SetIntensity(0.3f);
    viewer->AddLight(ambient);
    viewer->AddLight(key);
    viewer->AddLight(fill);
    viewer->SetLightOn();
}

// OpenGL sized formats (not declared by every platform's GL 1.1 header).
constexpr int kGlRgba8           = 0x8058;
constexpr int kGlDepth24Stencil8 = 0x88F0;

gp_Pnt ToPnt(Vec3 v) { return {v.x, v.y, v.z}; }
gp_Dir ToDir(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3   ToVec(const gp_XYZ& p) { return {p.X(), p.Y(), p.Z()}; }

Quantity_Color ToQuantity(const Color& c) {
    return Quantity_Color(static_cast<double>(c.r), static_cast<double>(c.g), static_cast<double>(c.b), Quantity_TOC_sRGB);
}

double SmoothStep(double t) { return t * t * (3.0 - 2.0 * t); }

/// Rotates `v` toward `w` by fraction t (both unit vectors).
Vec3 Slerp(Vec3 v, Vec3 w, double t) {
    const double dot = std::clamp(v.Dot(w), -1.0, 1.0);
    const double angle = std::acos(dot);
    if (angle < 1e-6) return v;
    if (kPi - angle < 1e-6) {
        // Opposite directions: turn around any perpendicular axis.
        Vec3 axis = std::abs(v.z) < 0.9 ? v.Cross({0, 0, 1}) : v.Cross({1, 0, 0});
        axis = axis.Normalized();
        const double a = angle * t;
        return (v * std::cos(a) + axis.Cross(v) * std::sin(a)).Normalized();
    }
    const double s = std::sin(angle);
    return (v * (std::sin((1 - t) * angle) / s) + w * (std::sin(t * angle) / s)).Normalized();
}

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
    GLFWwindow*                      window = nullptr;
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
    std::uint64_t      syncedRevision = static_cast<std::uint64_t>(-1);
    const kernel::Model* syncedModel = nullptr;
    std::optional<Vec2>  hoverPixel;
    std::optional<kernel::Bounds> bounds;

    ViewportColors colors;
    DisplayStyle   style = DisplayStyle::ShadedWithEdges;
    ObjectId       hovered;
    bool           dirty = true;

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
        drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(ToQuantity(colors.edges), Aspect_TOL_SOLID, 1.0));
        drawer->SetWireAspect(new Prs3d_LineAspect(ToQuantity(colors.edges), Aspect_TOL_SOLID, 1.2));
        drawer->SetLineAspect(new Prs3d_LineAspect(ToQuantity(colors.edges), Aspect_TOL_SOLID, 1.2));
        ais->SetTransparency(style == DisplayStyle::Transparent ? 0.55 : 0.0);

        const bool wire = style == DisplayStyle::Wireframe || style == DisplayStyle::HiddenLines;
        context->SetDisplayMode(ais, wire ? AIS_WireFrame : AIS_Shaded, false);
    }

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

bool Viewport::Init(void* glfwWindow) {
    Impl& m = *myImpl;
    m.window = static_cast<GLFWwindow*>(glfwWindow);

    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
    m.driver = new OpenGl_GraphicDriver(display, false);
    m.driver->ChangeOptions().buffersNoSwap = true;

    m.viewer = new V3d_Viewer(m.driver);
    SetUpLights(m.viewer);
    m.context = new AIS_InteractiveContext(m.viewer);
    m.context->SetPixelTolerance(4);

    // Pre-highlight (hover) and selection styles: a light-blue tint.
    for (auto type : {Prs3d_TypeOfHighlight_LocalDynamic, Prs3d_TypeOfHighlight_Dynamic}) {
        Handle(Prs3d_Drawer) s = m.context->HighlightStyle(type);
        s->SetColor(ToQuantity(m.colors.hover));
        s->SetDisplayMode(AIS_Shaded);
        s->SetTransparency(0.35f);
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
    params.TransparencyMethod = Graphic3d_RTM_BLEND_OIT;
    params.IsAntialiasingEnabled = true;

    m.occtWindow = new Aspect_NeutralWindow();
    m.occtWindow->SetVirtual(true);
    m.occtWindow->SetSize(m.fbWidth, m.fbHeight);
#if defined(_WIN32)
    m.occtWindow->SetNativeHandle(reinterpret_cast<Aspect_Drawable>(glfwGetWin32Window(m.window)));
    Aspect_RenderingContext glContext = glfwGetWGLContext(m.window);
#elif defined(__APPLE__)
    m.occtWindow->SetNativeHandle(reinterpret_cast<Aspect_Drawable>(glfwGetCocoaWindow(m.window)));
    Aspect_RenderingContext glContext = glfwGetNSGLContext(m.window);
#else
    m.occtWindow->SetNativeHandle(static_cast<Aspect_Drawable>(glfwGetX11Window(m.window)));
    Aspect_RenderingContext glContext = glfwGetGLXContext(m.window);
#endif
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
        m.context->Display(ais, AIS_Shaded, -1, false);
        m.context->Activate(ais, AIS_Shape::SelectionMode(TopAbs_FACE));
        m.shown.push_back({body.id, ais});
    }
    m.bounds = model.BoundingBox();
    m.view->SetComputedMode(m.style == DisplayStyle::HiddenLines);
    m.dirty = true;
}

void Viewport::Hover(std::optional<Vec2> pixel) {
    Impl& m = *myImpl;
    if (pixel == m.hoverPixel) return;
    m.hoverPixel = pixel;
    if (!pixel) {
        m.context->ClearDetected(false);
        m.dirty = true;
        return;
    }
    const auto px = static_cast<int>(std::lround(pixel->x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel->y * m.pixelScale));
    m.context->MoveTo(px, py, m.view, false);
    m.dirty = true;
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

    const Vec3 fromDir = (m.animFrom.center - m.animFrom.eye).Normalized();
    const Vec3 toDir = (m.animTo.center - m.animTo.eye).Normalized();
    const double fromDist = (m.animFrom.center - m.animFrom.eye).Length();
    const double toDist = (m.animTo.center - m.animTo.eye).Length();
    const Vec3 dir = Slerp(fromDir, toDir, s);
    const Vec3 center = m.animFrom.center + (m.animTo.center - m.animFrom.center) * s;
    const Vec3 up = Slerp(m.animFrom.up.Normalized(), m.animTo.up.Normalized(), s);
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
    return result;
}

} // namespace bz::render
