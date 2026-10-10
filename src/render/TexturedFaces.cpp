#include "render/TexturedFaces.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_TextureParams.hxx>
#include <Image_PixMap.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Presentation.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <cmath>
#include <cstring>

namespace bz::render {
namespace {

/// Graphic3d_Texture2D's constructor from a pixmap is protected in OCCT 7.x
/// (public in 8); this one works with both.
class PictureTexture : public Graphic3d_Texture2D {
public:
    explicit PictureTexture(const Handle(Image_PixMap)& pixmap) : Graphic3d_Texture2D(pixmap, Graphic3d_TOT_2D_MIPMAP) {}
};

/// Texture coordinates (mm on the part) of a point of the face at (u, v).
gp_Pnt2d Surface(const BRepAdaptor_Surface& s, double u, double v, const gp_Pnt& p, const gp_Dir& n) {
    switch (s.GetType()) {
        case GeomAbs_Plane: return {u, v};   // a plane's parameters are lengths
        case GeomAbs_Cylinder: return {u * s.Cylinder().Radius(), v};
        case GeomAbs_Cone: {
            const gp_Cone c = s.Cone();
            return {u * (c.RefRadius() + v * std::sin(c.SemiAngle())), v};
        }
        case GeomAbs_Sphere: {
            const double r = s.Sphere().Radius();
            return {u * r * std::cos(v), v * r};
        }
        case GeomAbs_Torus: {
            const gp_Torus t = s.Torus();
            return {u * (t.MajorRadius() + t.MinorRadius() * std::cos(v)), v * t.MinorRadius()};
        }
        default: break;
    }
    // Free-form: projected along the axis the surface faces most.
    const double ax = std::abs(n.X()), ay = std::abs(n.Y()), az = std::abs(n.Z());
    if (az >= ax && az >= ay) return {p.X(), p.Y()};
    if (ay >= ax) return {p.X(), p.Z()};
    return {p.Y(), p.Z()};
}

} // namespace

Handle(Graphic3d_Texture2D) MakePictureTexture(int width, int height, std::span<const std::uint8_t> rgba) {
    Handle(Image_PixMap) pixmap = new Image_PixMap();
    if (!pixmap->InitTrash(Image_Format_RGBA, static_cast<Standard_Size>(width), static_cast<Standard_Size>(height))) return nullptr;
    pixmap->SetTopDown(true);
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        std::memcpy(pixmap->ChangeRow(static_cast<Standard_Size>(y)), rgba.data() + row * static_cast<std::size_t>(y), row);
    Handle(Graphic3d_Texture2D) texture = new PictureTexture(pixmap);
    texture->GetParams()->SetRepeat(true);
    texture->GetParams()->SetModulate(true);   // lit and shaded like the rest of the part
    texture->GetParams()->SetFilter(Graphic3d_TOTF_TRILINEAR);
    return texture;
}

TexturedFaces::TexturedFaces(std::vector<TopoDS_Face> faces, Handle(Graphic3d_Texture2D) texture, double tile,
                             const Graphic3d_MaterialAspect& material, double transparency)
    : myFaces(std::move(faces)), myTexture(std::move(texture)), myTile(tile > 1e-6 ? tile : 1.0), myMaterial(material),
      myTransparency(transparency) {}

void TexturedFaces::Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& presentation,
                            const Standard_Integer) {
    int nodes = 0, triangles = 0;
    for (const TopoDS_Face& face : myFaces) {
        TopLoc_Location loc;
        if (const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc); !tri.IsNull()) {
            nodes += tri->NbNodes();
            triangles += tri->NbTriangles();
        }
    }
    if (triangles == 0) return;
    Handle(Graphic3d_ArrayOfTriangles) array =
        new Graphic3d_ArrayOfTriangles(nodes, triangles * 3, Graphic3d_ArrayFlags_VertexNormal | Graphic3d_ArrayFlags_VertexTexel);
    for (const TopoDS_Face& face : myFaces) {
        TopLoc_Location loc;
        const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;
        const gp_Trsf trsf = loc.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const BRepAdaptor_Surface surface(face, false);
        const int first = array->VertexNumber();
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            gp_Pnt p = tri->Node(i);
            gp_Dir n(0, 0, 1);
            double u = 0, v = 0;
            if (tri->HasUVNodes()) {
                const gp_Pnt2d uv = tri->UVNode(i);
                u = uv.X();
                v = uv.Y();
                gp_Pnt at;
                gp_Vec du, dv;
                surface.D1(u, v, at, du, dv);   // the face's own surface: already where the face is
                const gp_Vec cross = du.Crossed(dv);
                if (cross.Magnitude() > 1e-12) n = gp_Dir(cross);
            }
            if (reversed) n.Reverse();
            p.Transform(trsf);
            if (!tri->HasUVNodes()) n.Transform(trsf);
            const gp_Pnt2d mm = Surface(surface, u, v, p, n);
            array->AddVertex(p, n, gp_Pnt2d(mm.X() / myTile, -mm.Y() / myTile));
        }
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
            int a = 0, b = 0, c = 0;
            tri->Triangle(t).Get(a, b, c);
            if (reversed) std::swap(b, c);
            array->AddEdges(first + a, first + b, first + c);
        }
    }

    Handle(Graphic3d_AspectFillArea3d) aspect = new Graphic3d_AspectFillArea3d();
    aspect->SetInteriorStyle(Aspect_IS_SOLID);
    Graphic3d_MaterialAspect material = myMaterial;
    material.SetColor(Quantity_NOC_WHITE);   // the picture's own colours
    material.SetTransparency(static_cast<float>(myTransparency));
    aspect->SetFrontMaterial(material);
    aspect->SetBackMaterial(material);
    aspect->SetInteriorColor(Quantity_NOC_WHITE);
    aspect->SetTextureMap(myTexture);
    aspect->SetTextureMapOn();
    aspect->SetDistinguishOff();
    Handle(Graphic3d_Group) group = presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect);
    group->AddPrimitiveArray(array);
}

} // namespace bz::render
