#include "render/Viewport.h"

#include "core/Log.h"
#include "core/Profile.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/Measure.h"
#include "kernel/occt/ModelImpl.h"
#include "render/TexturedFaces.h"
#include "core/Image.h"

#include <AIS_InteractiveContext.hxx>
#include <BRepLib.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <map>
#include <AIS_ColoredDrawer.hxx>
#include <AIS_ColoredShape.hxx>
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
#include <Image_AlienPixMap.hxx>
#include <V3d_ImageDumpOptions.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Graphic3d_Mat4d.hxx>
#include <Graphic3d_Vec2.hxx>
#include <OpenGl_ArbFBO.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopExp.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_Texture.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_IsoAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <Select3D_SensitiveEntity.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopoDS.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#if defined(__ANDROID__)
#include <chrono>
#else
#include <GLFW/glfw3.h>   // glfwGetTime for camera animation
#endif

#include <algorithm>
#include <format>
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
/// A reference to `face` found again by a point inside it: the middle of
/// one of its display triangles (never on an edge shared with a neighbour).
std::optional<doc::FaceReference> FaceReferenceOf(const TopoDS_Face& face) {
    TopLoc_Location location;
    const Handle(Poly_Triangulation) mesh = BRep_Tool::Triangulation(face, location);
    if (mesh.IsNull() || mesh->NbTriangles() < 1) return std::nullopt;
    int a = 0, b = 0, c = 0;
    mesh->Triangle(1).Get(a, b, c);
    const gp_Trsf toModel = location.Transformation();
    const gp_XYZ sum = mesh->Node(a).Transformed(toModel).XYZ() + mesh->Node(b).Transformed(toModel).XYZ() +
                       mesh->Node(c).Transformed(toModel).XYZ();
    const Vec3 point = ToVec(sum / 3.0);
    const std::optional<Vec3> normal = kernel::occt::NormalAt(face, point);
    if (!normal) return std::nullopt;
    return doc::FaceReference{*normal, point, true};
}

/// The owner by which the face selection mode of `shape` picks `face`
/// (null if that mode was never computed or the face is not there).
Handle(SelectMgr_EntityOwner) FaceOwner(const Handle(AIS_InteractiveObject)& shape, const TopoDS_Face& face) {
    const int mode = AIS_Shape::SelectionMode(TopAbs_FACE);
    if (!shape->HasSelection(mode)) return nullptr;
    for (const Handle(SelectMgr_SensitiveEntity)& entity : shape->Selection(mode)->Entities()) {
        const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(entity->BaseSensitive()->OwnerId());
        if (!owner.IsNull() && owner->Shape().IsSame(face)) return Handle(SelectMgr_EntityOwner)(owner);
    }
    return nullptr;
}

std::optional<Frame> PlanarFaceFrame(const TopoDS_Face& face) {
    BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) return std::nullopt;
    const gp_Pln pln = surface.Plane();
    Vec3 normal = ToVec(pln.Axis().Direction().XYZ());
    if (!pln.Position().Direct()) normal = -normal;   // a left-handed (mirrored) frame: X × Y is opposite its axis
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

namespace {

/// How finely bodies are meshed for display, by quality level (0 best).
/// Finer than OCCT's default (20°): round faces look round.
void SetMeshPrecision(const Handle(Prs3d_Drawer)& drawer, int quality) {
    static constexpr double kAngle[] = {4.0, 8.0, 15.0}, kCoefficient[] = {0.0005, 0.001, 0.003};
    drawer->SetDeviationAngle(kAngle[quality] * kPi / 180.0);
    drawer->SetDeviationCoefficient(kCoefficient[quality]);
    // Not a "change" for OCCT: a new presentation with its own precision
    // would otherwise wipe the shape's triangles (ClearOnOwnDeflectionChange)
    // and mesh it all again. SetQuality cleans them itself when it changes.
    drawer->UpdatePreviousDeviationAngle();
    drawer->UpdatePreviousDeviationCoefficient();
}

} // namespace

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
        ObjectId                 body;
        Handle(AIS_ColoredShape) shape;   ///< coloured face by face where the look says so
    };
    std::vector<Shown> shown;
    /// Bodies shown a moment ago, hidden but kept with their meshes: closing
    /// an operation without applying it brings the old shapes back at once.
    std::vector<Shown> spare;
    static constexpr std::size_t kSpare = 4;
    std::vector<ObjectId> hiddenBodies;   ///< left out of `shown` (SetHiddenBodies)
    /// Cosmetic threads and the like: drawn, never picked.
    std::vector<Handle(AIS_Shape)> decorations;

    /// Measuring (see SetMeasuring).
    struct Measuring {
        bool                           on = false;
        std::array<TopoDS_Shape, 2>      picked;   ///< null: that slot is empty
        std::array<Handle(AIS_Shape), 2> marks;    ///< the picked entities, highlighted
        Viewport::Measured             result;
    } measuring;

    /// Selection modes of the shown bodies for the current state.
    void ActivateShown() {
        for (const auto& s : shown) {
            context->Deactivate(s.shape);
            // Faces covered with a picture are picked on their skin.
            if (const auto it = textured.find(s.shape.get()); it != textured.end())
                for (const auto& obj : it->second)
                    if (!Handle(TexturedFaces)::DownCast(obj).IsNull()) {
                        context->Deactivate(obj);
                        if (!original.active) context->Activate(obj, AIS_Shape::SelectionMode(TopAbs_FACE));
                    }
            if (original.active) continue;
            context->Activate(s.shape, AIS_Shape::SelectionMode(TopAbs_FACE));
            if (measuring.on) {
                context->Activate(s.shape, AIS_Shape::SelectionMode(TopAbs_EDGE));
                context->Activate(s.shape, AIS_Shape::SelectionMode(TopAbs_VERTEX));
            }
        }
        ShowSelected();
    }

    /// Faces the user chose (Viewport::SetSelectedFaces), highlighted as
    /// selected on the shown bodies. Not while picking on an original body.
    std::vector<SelectedFace> selectedFaces;
    void ShowSelected() {
        context->ClearSelected(false);
        dirty = true;
        if (original.active) return;
        for (const SelectedFace& f : selectedFaces)
            for (const auto& s : shown) {
                if (s.body != f.body) continue;
                const std::optional<TopoDS_Face> face = kernel::occt::ResolveAnyFace(s.shape->Shape(), f.face);
                if (!face) break;
                Handle(SelectMgr_EntityOwner) owner = FaceOwner(s.shape, *face);
                if (const auto it = textured.find(s.shape.get()); owner.IsNull() && it != textured.end())
                    for (const auto& obj : it->second)   // a face with a picture: on its skin
                        if (owner.IsNull()) owner = FaceOwner(obj, *face);
                if (!owner.IsNull() && !owner->IsSelected()) context->AddOrRemoveSelected(owner, false);
                break;
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
    doc::Look      look;                ///< colours of bodies and faces (Viewport::SetLook)
    std::uint64_t  lookRevision = ~std::uint64_t{0};
    const void*    lookOwner = nullptr;
    bool           dirty = true;

    /// Picking on the body as it was before an operation (fillet, shell;
    /// see BeginPickingOriginal).
    struct OriginalPicking {
        bool                            active = false;
        bool                            requested = false;   ///< Begin called since the last ExpirePickingOriginal
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

    /// Material for a colour and a finish: matte with a faint highlight,
    /// metal with a bright one, glass glossy. The ambient part is the colour
    /// itself, so faces turned away from the lights stay readable.
    static Graphic3d_MaterialAspect MaterialOf(const Color& colour, doc::Finish finish) {
        Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_Plastified);
        material.SetColor(ToQuantity(colour));
        const double ambient = finish == doc::Finish::Metal ? 0.75 : 1.0;
        material.SetAmbientColor(Quantity_Color(colour.r * ambient, colour.g * ambient, colour.b * ambient, Quantity_TOC_RGB));
        const double shine = finish == doc::Finish::Metal ? 0.55 : finish == doc::Finish::Glass ? 0.45 : 0.12;
        material.SetSpecularColor(Quantity_Color(shine, shine, shine, Quantity_TOC_RGB));
        material.SetShininess(finish == doc::Finish::Matte ? 0.1f : 0.6f);
        return material;
    }

    void ApplyStyle(const Handle(AIS_Shape)& ais, bool hoveredBody, const doc::Appearance* appearance = nullptr) const {
        Handle(Prs3d_Drawer) drawer = ais->Attributes();
        const Color body = hoveredBody ? colors.hover : appearance ? appearance->face.WithAlpha(1.0f) : colors.body;
        ais->SetColor(ToQuantity(body));
        ais->SetMaterial(MaterialOf(body, appearance ? appearance->finish : doc::Finish::Matte));
        const Color edgeColour = appearance ? appearance->edges : colors.edges;

        const bool edges = style == DisplayStyle::ShadedWithEdges || style == DisplayStyle::Transparent;
        drawer->SetFaceBoundaryDraw(edges);
        // Sharp edges and the tangent edges where a fillet starts and ends
        // (they show its size); not the seam along a cylinder, which is
        // smooth beyond tangency.
        drawer->SetFaceBoundaryUpperContinuity(GeomAbs_C1);
        // Finer tessellation than OCCT's default (20°): round faces look
        // round; coarser on the lower quality levels.
        SetMeshPrecision(drawer, quality);
        drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(ToQuantity(edgeColour), Aspect_TOL_SOLID, 1.0));
        // Wireframe and hidden-line styles draw edges in the edge colour (not
        // the body colour SetColor gave them) and no isolines.
        const Quantity_Color ink = ToQuantity(edgeColour);
        drawer->SetWireAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetLineAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetFreeBoundaryAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetUnFreeBoundaryAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        drawer->SetUIsoAspect(new Prs3d_IsoAspect(ink, Aspect_TOL_SOLID, 1.0, 0));
        drawer->SetVIsoAspect(new Prs3d_IsoAspect(ink, Aspect_TOL_SOLID, 1.0, 0));
        // "Скрытые линии": only the visible edges, in the edge colour.
        drawer->SetSeenLineAspect(new Prs3d_LineAspect(ink, Aspect_TOL_SOLID, 1.2));
        ais->SetTransparency(style == DisplayStyle::Transparent ? 0.55 : appearance ? 1.0 - appearance->face.a : 0.0);

        const bool wire = style == DisplayStyle::Wireframe || style == DisplayStyle::HiddenLines;
        context->SetDisplayMode(ais, wire ? AIS_WireFrame : AIS_Shaded, false);
    }

    int quality = 0;   ///< see Viewport::SetQuality

    /// A body's style with its look: the body's appearance, then the faces
    /// that have their own.
    /// Pictures of the look as textures, by picture name.
    std::map<std::string, Handle(Graphic3d_Texture2D)> textures;
    /// Faces covered with pictures, by the body presentation they belong to.
    std::map<const AIS_ColoredShape*, std::vector<Handle(AIS_InteractiveObject)>> textured;

    int textureLimit = 0;   ///< see Viewport::SetTextureLimit

    Handle(Graphic3d_Texture2D) TextureFor(const std::string& picture) {
        if (const auto it = textures.find(picture); it != textures.end()) return it->second;
        Handle(Graphic3d_Texture2D) texture;
        if (const auto bytes = look.pictures.find(picture); bytes != look.pictures.end())
            if (const auto image = DecodeImage(bytes->second)) {
                const Image fitted = FitWithin(*image, textureLimit);
                texture = MakePictureTexture(fitted.width, fitted.height, fitted.rgba);
            }
        textures[picture] = texture;
        return texture;
    }

    void ClearTextured(const AIS_ColoredShape* ais) {
        const auto it = textured.find(ais);
        if (it == textured.end()) return;
        for (const auto& obj : it->second) context->Remove(obj, false);
        textured.erase(it);
    }

    /// The shown body a picked object belongs to: the body itself or the
    /// skin over its faces with a picture. None for anything else.
    ObjectId BodyOf(const Handle(SelectMgr_SelectableObject)& picked) const {
        for (const auto& s : shown) {
            if (picked.get() == s.shape.get()) return s.body;
            if (const auto it = textured.find(s.shape.get()); it != textured.end())
                for (const auto& obj : it->second)
                    if (picked.get() == obj.get()) return s.body;
        }
        return {};
    }

    /// A body's style with its look: the body's appearance, then the faces
    /// that have their own; faces with a picture are hidden on the body and
    /// drawn textured over it (TexturedFaces), with their edges.
    void ApplyLook(const Shown& s) {
        ClearTextured(s.shape.get());
        const doc::Appearance* bodyLook = look.OfBody(s.body);
        ApplyStyle(s.shape, s.body == hovered, bodyLook);
        s.shape->ClearCustomAspects();
        if (style == DisplayStyle::Wireframe || style == DisplayStyle::HiddenLines) return;
        const TopoDS_Shape& shape = s.shape->Shape();

        std::map<std::string, std::vector<TopoDS_Face>> pictured;   // appearance id -> faces
        TopTools_IndexedMapOfShape ownLook;
        for (const doc::Look::FaceLook& f : look.faces) {
            if (f.body != s.body) continue;
            const doc::Appearance* a = look.Find(f.appearance);
            const std::optional<TopoDS_Face> face = a ? kernel::occt::ResolveAnyFace(shape, f.face) : std::nullopt;
            if (!face) continue;
            ownLook.Add(*face);
            if (a->HasPicture()) {
                pictured[a->id].push_back(*face);
                continue;
            }
            Handle(AIS_ColoredDrawer) aspects = s.shape->CustomAspects(*face);
            const Color colour = a->face.WithAlpha(1.0f);
            aspects->SetupOwnShadingAspect();
            aspects->ShadingAspect()->SetMaterial(MaterialOf(colour, a->finish));
            aspects->ShadingAspect()->SetColor(ToQuantity(colour));
            if (style != DisplayStyle::Transparent) aspects->ShadingAspect()->SetTransparency(1.0 - a->face.a);
        }
        if (bodyLook && bodyLook->HasPicture())
            for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next())
                if (!ownLook.Contains(e.Current())) pictured[bodyLook->id].push_back(TopoDS::Face(e.Current()));

        for (const auto& [id, faces] : pictured) {
            const doc::Appearance* a = look.Find(id);
            const Handle(Graphic3d_Texture2D) texture = a ? TextureFor(a->picture) : nullptr;
            if (texture.IsNull()) continue;   // picture missing: the body's colour stays
            for (const TopoDS_Face& face : faces) s.shape->CustomAspects(face)->SetHidden(true);
            const double transparency = style == DisplayStyle::Transparent ? 0.55 : 1.0 - a->face.a;
            Handle(TexturedFaces) skin = new TexturedFaces(faces, texture, a->pictureSize, MaterialOf(Color::Hex(0xFFFFFF), a->finish), transparency);
            // Picked as the faces it covers (hidden on the body), unless the
            // original body before an operation is what is picked now.
            context->Display(skin, 0, original.active ? -1 : AIS_Shape::SelectionMode(TopAbs_FACE), false);
            textured[s.shape.get()].push_back(skin);
            // Their edges: the sharp ones (not seams, not between tangent faces).
            if (style != DisplayStyle::ShadedWithEdges && style != DisplayStyle::Transparent) continue;
            BRepLib::EncodeRegularity(shape);
            TopoDS_Compound edges;
            BRep_Builder builder;
            builder.MakeCompound(edges);
            TopTools_IndexedMapOfShape seen;
            for (const TopoDS_Face& face : faces)
                for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
                    const TopoDS_Edge& edge = TopoDS::Edge(e.Current());
                    if (seen.Contains(edge) || BRep_Tool::IsClosed(edge, face) || BRep_Tool::MaxContinuity(edge) >= GeomAbs_G1) continue;
                    seen.Add(edge);
                    builder.Add(edges, edge);
                }
            Handle(AIS_Shape) lines = new AIS_Shape(edges);
            lines->SetColor(ToQuantity(a->edges));
            lines->SetWidth(1.0);
            context->Display(lines, AIS_WireFrame, -1, false);
            textured[s.shape.get()].push_back(lines);
        }
    }

    void ApplyStyleAll() {
        for (const Shown& s : shown) {
            ApplyLook(s);
            context->Redisplay(s.shape, false);
        }
        if (!selectedFaces.empty()) ShowSelected();   // redisplayed shapes come back unselected
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

namespace {
/// The clock StepAnimation's `now` comes from: glfwGetTime on the desktop;
/// on Android (no GLFW) a steady clock — android/AndroidMain uses the same.
double AnimationClock() {
#if defined(__ANDROID__)
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    return glfwGetTime();
#endif
}
} // namespace

Viewport::Viewport() : myImpl(std::make_unique<Impl>()) {}

Viewport::~Viewport() {
    Impl& m = *myImpl;
    if (!m.fbo.IsNull() && !m.driver.IsNull()) m.fbo->Release(m.GlContext().get());
}

bool Viewport::Init(const NativeGlSurface& surface) {
    Impl& m = *myImpl;

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__)
    // The window's own X connection, not a second one. OCCT asks "is my
    // context current?" by comparing its connection with the current one;
    // with a connection of its own the answer was always no, and it bound the
    // context to that connection before every 3D render. Under XWayland with
    // the NVIDIA driver the window then kept showing the last picture: the
    // program worked on, the screen froze once a document was open
    // (Hyprland, 2026-10).
    Handle(Aspect_DisplayConnection) display =
        surface.display ? new Aspect_DisplayConnection(static_cast<Aspect_XDisplay*>(surface.display)) : new Aspect_DisplayConnection();
#else
    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
#endif
    m.driver = new OpenGl_GraphicDriver(display, false);
#if defined(__ANDROID__)
    // EGL: OCCT must be told the display, context and configuration of the
    // app's context itself, or it refuses the window ("EGL does not provide
    // compatible configurations").
    if (!m.driver->InitEglContext(surface.display, surface.glContext, surface.eglConfig)) {
        log::Error("Viewport: OCCT did not take the EGL context");
        return false;
    }
#endif
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

void Viewport::PrepareMeshes(const kernel::Model& model, int quality) {
    Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
    SetMeshPrecision(drawer, std::clamp(quality, 0, 2));
    for (const kernel::Body& body : model.GetImpl().bodies) StdPrs_ToolTriangulatedShape::Tessellate(body.shape, drawer);
}

void Viewport::Sync(const kernel::Model& model) {
    Impl& m = *myImpl;
    if (&model == m.syncedModel && model.Revision() == m.syncedRevision) return;
    m.syncedModel = &model;
    m.syncedRevision = model.Revision();

    // A body whose shape did not change keeps its presentation: meshing and
    // the selection structures are the costly part, and most edits (a
    // sketch, a later operation) leave most bodies as they were.
    std::vector<Impl::Shown> previous = std::move(m.shown);
    m.shown.clear();
    for (const kernel::Body& body : model.GetImpl().bodies) {
        if (std::find(m.hiddenBodies.begin(), m.hiddenBodies.end(), body.id) != m.hiddenBodies.end()) continue;
        const auto same = std::find_if(previous.begin(), previous.end(), [&](const Impl::Shown& s) {
            return !s.shape.IsNull() && s.body == body.id && s.shape->Shape().IsEqual(body.shape);
        });
        if (same != previous.end()) {
            m.shown.push_back(*same);
            same->shape.Nullify();   // taken: not removed below
            continue;
        }
        const auto kept = std::find_if(m.spare.begin(), m.spare.end(), [&](const Impl::Shown& s) {
            return s.body == body.id && s.shape->Shape().IsEqual(body.shape);
        });
        if (kept != m.spare.end()) {
            m.ApplyLook(*kept);
            m.context->Display(kept->shape, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
            m.shown.push_back(*kept);
            m.spare.erase(kept);
            continue;
        }
        Handle(AIS_ColoredShape) ais = new AIS_ColoredShape(body.shape);
        m.ApplyLook({body.id, ais});
        m.context->Display(ais, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
        m.shown.push_back({body.id, ais});
    }
    for (const auto& s : previous) {
        if (s.shape.IsNull()) continue;
        m.ClearTextured(s.shape.get());
        m.context->Erase(s.shape, false);
        m.spare.push_back(s);
    }
    while (m.spare.size() > Impl::kSpare) {
        m.context->Remove(m.spare.front().shape, false);
        m.spare.erase(m.spare.begin());
    }
    for (const auto& d : m.decorations) m.context->Remove(d, false);
    m.decorations.clear();
    for (const auto& [id, shape] : model.GetImpl().decorations) {
        Handle(AIS_Shape) ais = new AIS_Shape(shape);
        ais->SetColor(ToQuantity(m.colors.edges));
        ais->SetWidth(1.4);
        SetMeshPrecision(ais->Attributes(), m.quality);
        m.context->Display(ais, AIS_WireFrame, -1, false);
        m.decorations.push_back(ais);
    }
    m.ActivateShown();
    // Picked entities belonged to the old shapes.
    if (!m.measuring.picked[0].IsNull() || !m.measuring.picked[1].IsNull()) ClearMeasure();
    m.bounds = model.BoundingBox();
    m.view->SetComputedMode(m.style == DisplayStyle::HiddenLines);
    m.dirty = true;
}

void Viewport::Hover(std::optional<Vec2> pixel) {
    prof::Scope profile("hover");
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
                                colors.radialBackground ? Aspect_GradientFillMethod_Elliptical : Aspect_GradientFillMethod_Vertical, false);
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
    for (const auto& s : m.spare) m.context->Remove(s.shape, false);
    m.spare.clear();
    for (const auto& s : m.shown) {
        BRepTools::Clean(s.shape->Shape());
        s.shape->SetToUpdate();
    }
    m.ApplyStyleAll();
}

bool Viewport::SameFace(ObjectId body, const doc::FaceReference& a, const doc::FaceReference& b) const {
    for (const auto& s : myImpl->shown) {
        if (s.body != body) continue;
        const auto fa = kernel::occt::ResolveAnyFace(s.shape->Shape(), a);
        const auto fb = kernel::occt::ResolveAnyFace(s.shape->Shape(), b);
        return fa && fb && fa->IsSame(*fb);
    }
    return false;
}

void Viewport::SetBackgroundPicture(int width, int height, std::span<const std::uint8_t> rgba) {
    Impl& m = *myImpl;
    Handle(Graphic3d_Texture2D) texture;
    if (width > 0 && height > 0) texture = MakePictureTexture(width, height, rgba);
    m.view->SetBackgroundImage(texture, Aspect_FM_STRETCH, false);
    m.dirty = true;
}

void Viewport::SetTextureLimit(int maxSide) {
    Impl& m = *myImpl;
    maxSide = std::max(0, maxSide);
    if (maxSide == m.textureLimit) return;
    m.textureLimit = maxSide;
    m.textures.clear();   // made again at the new size
    m.ApplyStyleAll();
}

void Viewport::SetLook(const doc::Look& look, const void* owner, std::uint64_t revision) {
    Impl& m = *myImpl;
    if (revision == m.lookRevision && owner == m.lookOwner) return;
    m.lookRevision = revision;
    m.lookOwner = owner;
    m.look = look;
    m.textures.clear();   // made again from this look's pictures
    m.ApplyStyleAll();
}

void Viewport::SetHiddenBodies(std::vector<ObjectId> bodies) {
    Impl& m = *myImpl;
    if (bodies == m.hiddenBodies) return;
    m.hiddenBodies = std::move(bodies);
    m.syncedRevision = static_cast<std::uint64_t>(-1);   // the next Sync shows or leaves them out
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

std::string Viewport::HandBackToWindow() {
    const Handle(OpenGl_Context) ctx = myImpl->GlContext();
    if (ctx.IsNull() || ctx->core11fwd == nullptr) return {};
    OpenGl_GlCore11Fwd& gl = *ctx->core11fwd;
    // What OCCT left: on Hyprland with an NVIDIA card the window froze on its
    // last picture once a document was open (2026-10), as if the interface
    // went to a framebuffer or buffer nobody shows. Logged when it changes.
    GLint drawFbo = 0, readFbo = 0, drawBuffer = 0;
    GLboolean mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    gl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    gl.glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    gl.glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    const bool scissor = gl.glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    const GLenum error = gl.glGetError();
    std::string state = std::format("draw framebuffer {}, read framebuffer {}, draw buffer 0x{:x}, colour writes {}{}{}{}, scissor {}, "
                                    "OpenGL error 0x{:x}",
                                    drawFbo, readFbo, drawBuffer, mask[0] ? 'r' : '-', mask[1] ? 'g' : '-', mask[2] ? 'b' : '-',
                                    mask[3] ? 'a' : '-', scissor ? "on" : "off", error);
    // The window's own framebuffer and its back buffer, everything written.
    if (ctx->arbFBO != nullptr) ctx->arbFBO->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ctx->SetDrawBuffer(GL_BACK);   // OCCT's own call: OCCT 7.6 has no glDrawBuffer in core11fwd
    gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.glDisable(GL_SCISSOR_TEST);
    return state;
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

std::optional<Image> Viewport::Snapshot(const CameraState& camera, int width, int height) {
    Impl& m = *myImpl;
    if (m.GlContext().IsNull()) m.view->Redraw();   // the context comes with the first redraw
    if (m.GlContext().IsNull() || width < 16 || height < 16) return std::nullopt;
    const CameraState before = Camera();
    SetCamera(camera);
    Image_AlienPixMap pixels;
    V3d_ImageDumpOptions options;
    options.Width      = width;
    options.Height     = height;
    options.BufferType = Graphic3d_BT_RGBA;
    bool done = false;
    try {
        done = m.view->ToPixMap(pixels, options);
    } catch (const Standard_Failure& e) {
        log::Error("Viewport: snapshot failed: {}", e.GetMessageString());
    }
    SetCamera(before);
    if (!done || pixels.IsEmpty()) return std::nullopt;
    Image out;
    out.width  = static_cast<int>(pixels.SizeX());
    out.height = static_cast<int>(pixels.SizeY());
    out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4);
    // The bytes as drawn (the window shows the same), rows top to bottom.
    const bool rgba = pixels.Format() == Image_Format_RGBA;
    const bool bgra = pixels.Format() == Image_Format_BGRA;
    if (!rgba && !bgra) return std::nullopt;
    for (int y = 0; y < out.height; ++y) {
        // Row(0) is the top row whichever way the bytes lie (Image_PixMap
        // applies its own top-down order).
        const std::uint8_t* from = pixels.Row(static_cast<std::size_t>(y));
        std::uint8_t* to = &out.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) * 4];
        for (int x = 0; x < out.width; ++x, from += 4, to += 4) {
            to[0] = bgra ? from[2] : from[0];
            to[1] = from[1];
            to[2] = bgra ? from[0] : from[2];
            to[3] = 255;
        }
    }
    return out;
}

void Viewport::Orbit(double yaw, double pitch, Vec3 pivot, bool turntable) {
    CameraState c = Camera();
    const Vec3 dir = (c.center - c.eye).Normalized();
    const Vec3 right = dir.Cross(c.up).Normalized();

    gp_Trsf pitchRot, yawRot;
    pitchRot.SetRotation(gp_Ax1(ToPnt(pivot), ToDir(right)), -pitch);
    // Free: sideways drag turns about the screen's vertical, whatever the
    // view (a model seen from the top no longer spins in its own plane).
    yawRot.SetRotation(gp_Ax1(ToPnt(pivot), turntable ? gp::DZ() : ToDir(c.up)), -yaw);
    const gp_Trsf t = yawRot.Multiplied(pitchRot);

    const gp_Pnt eye = ToPnt(c.eye).Transformed(t);
    const gp_Pnt center = ToPnt(c.center).Transformed(t);
    const gp_Dir up = ToDir(c.up).Transformed(t);
    SetCamera({ToVec(eye.XYZ()), ToVec(center.XYZ()), ToVec(up.XYZ()), c.scale});
}

void Viewport::Roll(double angle, Vec3 pivot) {
    CameraState c = Camera();
    const Vec3 dir = (c.center - c.eye).Normalized();
    // The camera turns about its own line of sight (away from the viewer):
    // a positive turn of the camera is a counter-clockwise turn of the model.
    gp_Trsf t;
    t.SetRotation(gp_Ax1(ToPnt(pivot), ToDir(dir)), angle);
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

CameraState Viewport::FitCamera(Vec3 direction, Vec3 up, const Box3& extra, double margin, double aspect) const {
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
    if (aspect <= 0) aspect = m.width / m.height;
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
    m.animStart = AnimationClock();
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
    result.body = m.BodyOf(owner->Selectable());

    if (const Handle(StdSelect_BRepOwner) brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
        !brep.IsNull() && brep->Shape().ShapeType() == TopAbs_FACE) {
        result.plane = PlanarFaceFrame(TopoDS::Face(brep->Shape()));
        if (const auto normal = kernel::occt::NormalAt(TopoDS::Face(brep->Shape()), result.point))
            result.face = doc::FaceReference{*normal, result.point, true};
    }
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

// --- faces chosen by the user ----------------------------------------------------

void Viewport::SetSelectedFaces(const std::vector<SelectedFace>& faces) {
    Impl& m = *myImpl;
    if (faces == m.selectedFaces) return;
    m.selectedFaces = faces;
    m.ShowSelected();
}

std::vector<SelectedFace> Viewport::FacesInRect(Vec2 a, Vec2 b, bool crossing) {
    Impl& m = *myImpl;
    std::vector<SelectedFace> found;
    if (m.shown.empty() || m.original.active) return found;
    const auto px = [&](double v) { return static_cast<int>(std::lround(v * m.pixelScale)); };
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->AllowOverlapDetection(crossing);
    selector->Pick(px(std::min(a.x, b.x)), px(std::min(a.y, b.y)), px(std::max(a.x, b.x)), px(std::max(a.y, b.y)), m.view);
    selector->AllowOverlapDetection(false);
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(StdSelect_BRepOwner) brep = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(i));
        if (brep.IsNull() || brep->Shape().ShapeType() != TopAbs_FACE) continue;
        const ObjectId body = m.BodyOf(brep->Selectable());
        if (!body) continue;
        if (const auto ref = FaceReferenceOf(TopoDS::Face(brep->Shape()))) found.push_back({body, *ref});
    }
    return found;
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

bool Viewport::MeasurePick(Vec2 pixel, int slot) {
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
    const auto s = static_cast<std::size_t>(std::clamp(slot, 0, 1));
    ClearMeasure(static_cast<int>(s));

    const TopoDS_Shape entity = owner->Shape();
    const kernel::EntityInfo info = kernel::occt::Describe(entity, ToVec(selector->PickedPoint(1).XYZ()));
    ms.picked[s] = entity;
    (s == 0 ? ms.result.first : ms.result.second) = info;
    if (!ms.picked[0].IsNull() && !ms.picked[1].IsNull()) ms.result.between = kernel::occt::Measure(ms.picked[0], ms.picked[1]);

    Handle(AIS_Shape) mark = new AIS_Shape(entity);
    mark->SetColor(ToQuantity(m.colors.selection));
    mark->SetWidth(3.5);
    mark->SetTransparency(entity.ShapeType() == TopAbs_FACE ? 0.6 : 0.0);
    mark->Attributes()->SetFaceBoundaryDraw(false);
    mark->SetZLayer(Graphic3d_ZLayerId_Topmost);
    m.context->Display(mark, entity.ShapeType() == TopAbs_FACE ? AIS_Shaded : AIS_WireFrame, -1, false);
    ms.marks[s] = mark;
    m.dirty = true;
    return true;
}

void Viewport::ClearMeasure(int slot) {
    Impl& m = *myImpl;
    Impl::Measuring& ms = m.measuring;
    const auto s = static_cast<std::size_t>(std::clamp(slot, 0, 1));
    if (!ms.marks[s].IsNull()) m.context->Remove(ms.marks[s], false);
    ms.marks[s].Nullify();
    ms.picked[s].Nullify();
    (s == 0 ? ms.result.first : ms.result.second).reset();
    ms.result.between.reset();
    m.dirty = true;
}

void Viewport::ClearMeasure() {
    ClearMeasure(0);
    ClearMeasure(1);
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
    prof::Scope profile("pick-setup");
    Impl& m = *myImpl;
    Impl::OriginalPicking& o = m.original;
    o.requested = true;

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

void Viewport::ExpirePickingOriginal() {
    Impl::OriginalPicking& o = myImpl->original;
    if (o.active && !o.requested) EndPickingOriginal();
    o.requested = false;
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
    if (!m.original.active || (m.original.target != PickTarget::Faces && m.original.target != PickTarget::EdgesAndFaces))
        return std::nullopt;
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

bool Viewport::OriginalUnder(Vec2 pixel) {
    Impl& m = *myImpl;
    if (!m.original.active) return false;
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(static_cast<int>(std::lround(pixel.x * m.pixelScale)), static_cast<int>(std::lround(pixel.y * m.pixelScale)), m.view);
    return selector->NbPicked() > 0;
}

std::optional<doc::FaceReference> Viewport::PickFace(Vec2 pixel, bool curvedToo) {
    Impl& m = *myImpl;
    if (!m.original.active) return std::nullopt;
    const auto px = static_cast<int>(std::lround(pixel.x * m.pixelScale));
    const auto py = static_cast<int>(std::lround(pixel.y * m.pixelScale));
    const Handle(StdSelect_ViewerSelector3d)& selector = m.context->MainSelector();
    selector->Pick(px, py, m.view);
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(i));
        if (owner.IsNull() || owner->Shape().ShapeType() != TopAbs_FACE) continue;
        const TopoDS_Face& face = TopoDS::Face(owner->Shape());
        const Vec3 point = ToVec(selector->PickedPoint(i).XYZ());
        if (const std::optional<Vec3> normal = kernel::occt::PlanarNormal(face)) return doc::FaceReference{*normal, point};
        if (!curvedToo) return std::nullopt;   // the nearest face is curved
        const std::optional<Vec3> normal = kernel::occt::NormalAt(face, point);
        if (!normal) return std::nullopt;
        return doc::FaceReference{*normal, point, true};
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
