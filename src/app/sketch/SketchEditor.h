#pragma once
// Sketch mode: drawing tools, constraints, dimensions, selection and
// dragging, with CAD feedback (snaps, inference, fully defined colours).
//
// Every finished action is one undo step: the sketch feature is edited in
// place for live feedback and then committed with Session::CommitInPlaceEdit.

#include "app/AppContext.h"
#include "app/sketch/SketchPick.h"
#include "document/SketchFeature.h"
#include "sketch/Move.h"
#include "sketch/Solver.h"

#include <memory>
#include "render/Viewport.h"

#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace bz::app {

enum class SketchTool : unsigned char {
    Select, Line, Rectangle, Circle, Arc, Point, Project, Trim, Move, Rotate, Dimension,
    Coincident, Horizontal, Vertical, Parallel, Perpendicular, Equal, Tangent, Fix, Midpoint, Concentric,
};

/// Ways to draw with one tool, chosen in the tool panel.
enum class RectangleMode : unsigned char {
    Corners, Centre, ThreePoints, ThreePointsCentre, Polygon,
    SlotCentres, SlotOverall, SlotCentre, SlotArc3, SlotArcCentre,
};
inline bool IsSlot(RectangleMode m) { return m >= RectangleMode::SlotCentres; }
inline bool IsArcSlot(RectangleMode m) { return m == RectangleMode::SlotArc3 || m == RectangleMode::SlotArcCentre; }
enum class CircleMode : unsigned char { CentreRadius, ThreePoints };
enum class ArcMode : unsigned char { CentreEnds, ThreePoints };

/// Line type of what is drawn: основная (makes profiles), вспомогательная
/// (helps, makes nothing), осевая (an axis for revolve and coil; lines only).
enum class LineStyle : unsigned char { Normal, Construction, Centerline };

/// The tool panel's settings. Typed sizes lock the cursor while drawing
/// and become dimensions of what is drawn.
struct DrawOptions {
    LineStyle     style = LineStyle::Normal;
    RectangleMode rectangle = RectangleMode::Corners;
    CircleMode    circle = CircleMode::CentreRadius;
    ArcMode       arc = ArcMode::CentreEnds;
    int           polygonSides = 6;
    std::string   sides = "6";      ///< polygon side count as typed
    bool          polygonInscribed = true;
    std::string   length, angle;    ///< line
    std::string   width, height;    ///< rectangle
    std::string   size;             ///< circle / polygon diameter, arc radius
    std::string   moveX, moveY;     ///< move
    std::string   turn;             ///< rotate
};

struct SketchToolInfo {
    SketchTool  tool;
    const char* label;     ///< "Отрезок"
    const char* icon;      ///< "sk-line"
    const char* hint;      ///< first step hint for the status bar
};

/// All tools in ribbon order.
std::span<const SketchToolInfo> SketchToolList();
const SketchToolInfo& InfoOf(SketchTool tool);

class SketchEditor {
public:
    // --- entering and leaving --------------------------------------------
    /// Creates a sketch on `placement` (undoable) and starts editing it.
    void CreateAndEdit(AppContext& app, Session& session, const doc::SketchPlacement& placement);
    void Edit(AppContext& app, Session& session, ObjectId sketch);
    void Finish(AppContext& app, Session& session);
    /// Turns the camera to look straight at the sketch plane (F6).
    void LookAtSketch(AppContext& app, Session& session);
    /// Zooms out (keeping the direction) when the sketch left the screen.
    void KeepSketchInView(AppContext& app, Session& session);

    // --- tools ------------------------------------------------------------
    SketchTool Tool() const { return myTool; }
    /// Takes back the click a drawing tool took from the current press (the
    /// radial menu opened on that press instead).
    void DropPressClick();
    void SetTool(AppContext& app, Session& session, SketchTool tool);
    /// Esc: drops the current step, then the tool, then leaves the sketch.
    void Escape(AppContext& app, Session& session);
    void DeleteSelection(Session& session);
    void ToggleConstruction(Session& session);
    /// Makes the selected lines осевые (or normal again).
    void ToggleCenterline(Session& session);
    bool HasSelection() const { return !mySelection.empty(); }

    /// A drawing tool is active: the left area shows its panel.
    bool HasToolPanel() const;
    /// Ways to draw and typed sizes of the active drawing tool.
    void DrawToolPanel(AppContext& app, Session& session, float width);

    /// Input and drawing inside the viewport, once per frame.
    void Frame(AppContext& app, Session& session, const ViewportFrame& vf);

    /// Status bar: what to do next.
    std::string Hint(const Session& session) const;
    /// Cursor position in sketch coordinates, when over the plane.
    std::optional<Vec2> Cursor() const { return myCursor; }

    /// Forgets transient state (when switching documents).
    void Reset();

private:
    doc::SketchFeature* Active(Session& session) const;

    // Tool handlers, called from Frame().
    void HandleSelect(AppContext& app, Session& session, const SketchView& view);
    void HandleDrawing(AppContext& app, Session& session, const SketchView& view);
    /// "Проецировать": a click on a model edge adds its projection.
    void HandleProject(AppContext& app, Session& session, const SketchView& view);
    /// "Обрезка": the piece under the cursor is shown, a click removes it.
    void HandleTrim(AppContext& app, Session& session, const SketchView& view);
    /// "Перенос" / "Поворот": the selection follows two (three) clicks.
    void HandleMotion(AppContext& app, Session& session, const SketchView& view);
    /// The motion the clicks so far and the cursor describe; nullopt when not ready.
    std::optional<sketch::Motion> PendingMotion(const UnitSystem& units, bool withCursor) const;
    /// Moves the selection as one undo step.
    void ApplyMotion(AppContext& app, Session& session, const sketch::Motion& motion);
    void HandleDimension(AppContext& app, Session& session, const SketchView& view);
    void HandleConstraint(AppContext& app, Session& session, const SketchView& view);
    /// Constraint tools' panel: the picked elements, each can be dropped.
    void DrawPicks(AppContext& app, Session& session, float width);
    void HandleInlineEdit(AppContext& app, Session& session, const SketchView& view);

    // Building blocks.
    /// A clicked position: an existing point to reuse or a new one.
    struct PointRef {
        ObjectId id;
        Vec2     pos;
        ObjectId onCurve;
        ObjectId midOf;   ///< a line whose midpoint this is
    };
    PointRef RefFromSnap(const Snap& snap) const;
    ObjectId Resolve(doc::Document& doc, sketch::Sketch& sk, const PointRef& ref);
    void FinishDrawingStep(AppContext& app, Session& session, const Snap& snap);
    /// Clicks the active tool needs, in its current mode.
    std::size_t ClicksNeeded() const;
    /// Moves the cursor position to honour typed sizes; true when it moved.
    bool ApplyTypedSizes(const UnitSystem& units, Vec2& pos) const;
    /// A typed size to turn into a dimension; its label goes away from `inside`.
    struct TypedDimension {
        sketch::Constraint  constraint;
        std::string         text;
        std::optional<Vec2> inside;
    };
    /// Adds dimensions for typed sizes to the shape just drawn.
    void AddTypedDimensions(doc::Document& doc, sketch::Sketch& sk, const std::vector<TypedDimension>& dims);

    /// Applies a constraint as one undo step if it is valid, not redundant
    /// and solvable. Returns false and explains why otherwise.
    bool AddConstraint(AppContext& app, Session& session, sketch::Constraint constraint, const char* label);
    void TryApplyToSelection(AppContext& app, Session& session);
    void StartInlineEdit(const sketch::Constraint& c);
    void DrawPreview(AppContext& app, const SketchView& view, const sketch::Sketch& sk, const UnitSystem& units);
    /// The shape being drawn, from the clicks so far to the cursor.
    void DrawShapePreview(AppContext& app, const SketchView& view, const UnitSystem& units) const;

    SketchTool myTool = SketchTool::Select;
    double     myLastClickTime = -1.0;   ///< press time of the last drawing click (see DropPressClick)

    // drawing tools
    DrawOptions           myOptions;
    std::string           mySidesError;          ///< polygon side count that is not a whole number
    double                myMmPerPixel = 0.1;   ///< of the last frame, to place automatic dimension labels
    std::vector<PointRef> myClicks;
    Snap                  mySnap;
    std::optional<Vec2>   myCursor;

    // selection, picking
    std::unordered_set<ObjectId> mySelection;
    std::vector<ObjectId>        myPicks;
    SketchHit                    myHover;

    // dragging in the select tool
    std::unique_ptr<doc::Feature> myDragBefore;
    ObjectId                      myDragEntity;
    Vec2                          myDragStart;
    Vec2                          myDragLabelStart;   ///< dimension label position at drag start
    std::vector<sketch::Drag>     myDragOrigins;   ///< initial positions
    bool                          myDragging = false;
    bool                          myPressedOnEntity = false;

    /// Camera before entering the sketch; Finish returns to its direction.
    std::optional<render::CameraState> myCameraBeforeSketch;

    // inline dimension value editor
    ObjectId    myEditDimension;
    std::string myEditText;
    bool        myEditFocus = false;
};

} // namespace bz::app
