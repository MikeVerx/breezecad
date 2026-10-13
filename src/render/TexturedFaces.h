#pragma once
// Faces covered with a picture (an appearance with a picture): a mesh of
// their triangles with texture coordinates in millimetres, so the picture
// keeps its size on the part, wraps round cylinders, cones and spheres,
// and repeats edge to edge where the part is larger than one copy.

#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_Texture2D.hxx>
#include <TopoDS_Face.hxx>

#include <cstdint>
#include <span>
#include <vector>

namespace bz::render {

/// A texture from RGBA pixels (rows top to bottom), repeating, with mip-maps.
Handle(Graphic3d_Texture2D) MakePictureTexture(int width, int height, std::span<const std::uint8_t> rgba);

class TexturedFaces : public AIS_InteractiveObject {
public:
    /// `tile`: millimetres one copy of the picture covers on the part.
    TexturedFaces(std::vector<TopoDS_Face> faces, Handle(Graphic3d_Texture2D) texture, double tile,
                  const Graphic3d_MaterialAspect& material, double transparency);

    bool AcceptDisplayMode(const Standard_Integer mode) const override { return mode == 0; }

private:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager, const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer mode) override;
    /// The faces are hidden on the body (this draws them instead), and a
    /// hidden face is not picked there: they are picked here, as the same
    /// faces, in the face selection mode.
    void ComputeSelection(const Handle(SelectMgr_Selection)& selection, const Standard_Integer mode) override;

    std::vector<TopoDS_Face>    myFaces;
    Handle(Graphic3d_Texture2D) myTexture;
    double                      myTile;
    Graphic3d_MaterialAspect    myMaterial;
    double                      myTransparency;
};

} // namespace bz::render
