#pragma once
// The 3D viewport. Renders the model with OCCT into an OpenGL texture that
// the UI shows as an image and draws its overlays on (sketch, dimensions,
// view cube). This header is free of OCCT types.

#include "core/Color.h"
#include "core/Id.h"
#include "core/Math.h"
#include "core/NativeSurface.h"
#include "core/ViewProjection.h"
#include "document/EdgeReference.h"
#include "document/Appearance.h"
#include "document/FaceReference.h"
#include "kernel/Measure.h"

#include <array>
#include <cstdint>
#include <span>
#include <memory>
#include <optional>
#include <vector>

namespace bz::kernel {
class Model;
}

namespace bz::render {

/// Camera pose. Orthographic: `scale` is the visible height in mm.
struct CameraState {
    Vec3   eye{1, -1, 1};
    Vec3   center{0, 0, 0};
    Vec3   up{0, 0, 1};
    double scale = 200.0;
};

enum class DisplayStyle : unsigned char { Shaded, ShadedWithEdges, Wireframe, HiddenLines, Transparent };

struct ViewportColors {
    Color backgroundTop    = Color::Hex(0xF7F8FA);
    Color backgroundBottom = Color::Hex(0xE3E7EC);
    Color body             = Color::Hex(0xC7CDD5);
    Color edges            = Color::Hex(0x3D4552);
    Color hover            = Color::Hex(0x38A9E8);
    Color selection        = Color::Hex(0x1D4ED8);
    Color interference     = Color::Hex(0xE5484D);
    bool  radialBackground = false;   ///< backgroundTop in the middle, backgroundBottom at the edges
};

/// A translucent square drawn in the scene, hidden where the body is in
/// front of it (origin and work planes).
struct PlaneQuad {
    std::array<Vec3, 4> corners;
    Color               fill;
    Color               edge;
    float               edgeWidth = 1.2f;

    bool operator==(const PlaneQuad&) const = default;
};

/// What BeginPickingOriginal makes pickable.
enum class PickTarget : unsigned char { Edges, Faces, EdgesAndFaces, Vertices };

/// What lies under the cursor.
struct PickResult {
    Vec3                 point;
    ObjectId             body;
    std::optional<Frame> plane;   ///< set when the picked face is planar
    std::optional<doc::FaceReference> face;   ///< the face clicked (curved too), found again by its point
};

/// A face of a shown body chosen by the user in the view (Viewport::SetSelectedFaces).
struct SelectedFace {
    ObjectId           body;
    doc::FaceReference face;   ///< any face, found again by its point (FaceReference::curved)

    bool operator==(const SelectedFace&) const = default;
};

class Viewport {
public:
    Viewport();
    ~Viewport();
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    /// Renders with the window's OpenGL 3.3+ context, which must be current.
    bool Init(const NativeGlSurface& surface);

    /// Size of the scene area in logical pixels and the framebuffer scale.
    void SetSize(double width, double height, double pixelScale);

    /// Re-creates displayed bodies if the model (or which model) changed.
    void Sync(const kernel::Model& model);
    /// Meshes the model's bodies as Sync would at this quality level. Safe
    /// on another thread for a model no viewport shows yet: Sync then finds
    /// the triangles ready.
    static void PrepareMeshes(const kernel::Model& model, int quality);
    /// Pre-highlights the face under the cursor (nullopt: nothing).
    void Hover(std::optional<Vec2> pixel);
    void SetColors(const ViewportColors& colors);
    /// A static picture behind the model (stretched to the view); 0x0 removes it.
    void SetBackgroundPicture(int width, int height, std::span<const std::uint8_t> rgba);
    void SetDisplayStyle(DisplayStyle style);
    /// Graphics quality: 0 high (smooth edges, fine round faces), 1
    /// balanced, 2 for weak computers (no smoothing, coarse round faces).
    void SetQuality(int level);
    void SetHoveredBody(ObjectId body);
    /// Bodies not drawn (and not picked): those of operations the user hid.
    /// The model keeps them; only the view leaves them out.
    void SetHiddenBodies(std::vector<ObjectId> bodies);
    /// Both references point at the same face of the shown body `body`.
    bool SameFace(ObjectId body, const doc::FaceReference& a, const doc::FaceReference& b) const;
    /// Colours of bodies and faces of the document `owner` (any pointer that
    /// tells documents apart); applied when it or `revision` changes.
    void SetLook(const doc::Look& look, const void* owner, std::uint64_t revision);

    /// Renders into the texture. Cheap when nothing changed.
    void Render();
    /// OpenGL texture with the last rendered frame (bottom-up rows).
    unsigned TextureId() const;
    /// True when something changed that needs Render().
    bool NeedsRender() const;

    // --- camera ----------------------------------------------------------
    ViewProjection Projection() const;
    CameraState    Camera() const;
    void           SetCamera(const CameraState& camera);

    /// Orbit about `pivot`, angles in radians: `pitch` around the screen's
    /// horizontal axis; `yaw` around the screen's vertical axis (free, as in
    /// Inventor and KOMPAS) or, with `turntable`, around world Z.
    void Orbit(double yaw, double pitch, Vec3 pivot, bool turntable = false);
    void Pan(double dxPixels, double dyPixels);
    /// Zooms by `factor` (>1 closer) keeping the point under the cursor still.
    void ZoomAt(double factor, Vec2 pixel);

    /// Camera looking along `direction` with `up`, fitted to the model and
    /// to `extra` (things the UI draws itself, e.g. the sketch being edited).
    CameraState FitCamera(Vec3 direction, Vec3 up, const Box3& extra = {}, double margin = 1.15) const;
    /// Centre of the model's bounding box (origin when empty).
    Vec3 ModelCenter() const;
    /// Bounding box of the shown model (empty when there are no bodies).
    Box3 ModelBounds() const;

    /// Smoothly moves the camera to `target` over `seconds` (0 = jump).
    void AnimateTo(const CameraState& target, double seconds);
    /// Advances an animation; returns true while it is running.
    bool StepAnimation(double now);

    std::optional<PickResult> Pick(Vec2 pixel);

    // --- faces chosen by the user ---------------------------------------------
    /// Faces drawn in the selection colour on the shown bodies (a face that
    /// is no longer there is skipped). Cheap when unchanged.
    void SetSelectedFaces(const std::vector<SelectedFace>& faces);
    /// Faces of the shown bodies inside a screen rectangle (corners in any
    /// order, logical pixels): wholly inside it, or with `crossing` every
    /// face it touches, as Inventor's window and crossing selection.
    std::vector<SelectedFace> FacesInRect(Vec2 a, Vec2 b, bool crossing);

    /// Shows where the bodies of `model` overlap (Model::Interferences),
    /// over everything else; nullptr hides it. Cheap when unchanged.
    void ShowInterferences(const kernel::Model* model);

    /// Outlines of a pattern's or mirror's sources (selection colour) and
    /// copies (hover colour) over the model while its panel is open;
    /// nullptr hides them. Cheap when unchanged.
    void ShowCopyPreview(const kernel::Model* model, ObjectId feature);

    // --- measuring ---------------------------------------------------------
    /// While on, vertices, edges and faces of the shown bodies are hovered
    /// and picked for measuring (not while picking on an original body).
    void SetMeasuring(bool on);
    /// Picks the entity under the cursor: the first one, then the second,
    /// then the first again. Returns false when nothing is there.
    bool MeasurePick(Vec2 pixel);
    void ClearMeasure();
    struct Measured {
        std::optional<kernel::EntityInfo>  first, second;
        std::optional<kernel::Measurement> between;   ///< when both are picked
    };
    const Measured& Measurement() const;

    /// Cuts the view with a plane: what lies on the side its normal points
    /// to is hidden, and the cut faces are filled. nullopt: no section.
    /// Cheap when unchanged.
    void SetSection(const std::optional<Frame>& plane);

    /// Planes to show this frame; cheap when they did not change. They are
    /// never picked (the UI hit-tests them itself).
    void SetPlanes(const std::vector<PlaneQuad>& planes);

    // --- picking on the body before an operation (fillet, shell) ------
    /// Makes the bodies as they were before feature number `featureIndex`
    /// pickable (their edges or their faces); they are drawn as lines over
    /// the shown result, which is not pickable meanwhile. Cheap to call
    /// every frame.
    void BeginPickingOriginal(const kernel::Model& model, std::size_t featureIndex, PickTarget target);
    void EndPickingOriginal();
    /// Once a frame, before the editors draw: ends the picking nobody asked
    /// for since the last call. (Ending it whenever an editor does not say
    /// it picks would rebuild the targets every frame for one that does.)
    void ExpirePickingOriginal();
    /// Edge under the cursor (PickTarget::Edges), described for storing.
    std::optional<doc::EdgeReference> PickEdge(Vec2 pixel);
    /// Every edge of the face under the cursor (PickTarget::EdgesAndFaces,
    /// when no edge is nearer), described for storing.
    std::vector<doc::EdgeReference> PickFaceEdges(Vec2 pixel);
    /// A cylindrical face under the cursor (PickTarget::Faces): its axis,
    /// radius and the direction from the axis to the picked point.
    struct CylinderPick {
        Vec3   axisPoint, axisDir, radial;
        double radius = 0;
    };
    std::optional<CylinderPick> PickCylinder(Vec2 pixel);
    /// Vertex under the cursor (PickTarget::Vertices), model space, with an
    /// edge ending there (to find the vertex again after a rebuild).
    struct VertexPick {
        Vec3                              point;
        std::optional<doc::EdgeReference> edge;
    };
    std::optional<VertexPick> PickVertex(Vec2 pixel);
    /// Flat face under the cursor (PickTarget::Faces); nullopt for a curved one.
    /// The flat face under `pixel` (or, with `curvedToo`, any face: a curved
    /// one comes as FaceReference::curved).
    std::optional<doc::FaceReference> PickFace(Vec2 pixel, bool curvedToo = false);
    /// Some part of the body being picked on (BeginPickingOriginal) is under `pixel`.
    bool OriginalUnder(Vec2 pixel);
    /// True when both references point at the same face of the original
    /// body (a second click on a face drops it).
    bool SameOriginalFace(const doc::FaceReference& a, const doc::FaceReference& b) const;
    /// Edges and faces drawn as chosen while picking.
    void SetMarkedEdges(const std::vector<doc::EdgeReference>& edges);
    void SetMarkedFaces(const std::vector<doc::FaceReference>& faces);

    struct Impl;

private:
    std::unique_ptr<Impl> myImpl;
};

} // namespace bz::render
